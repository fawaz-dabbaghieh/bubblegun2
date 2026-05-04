#include "graph/packed_graph.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#include "fs/Reader.h"
#include "fs/gfa_line_parsers.h"

namespace bubblegun::graph {

using bubblegun::fs::LinkFields;
using bubblegun::fs::Reader;
using bubblegun::fs::SegmentFields;

PackedGraph PackedGraph::load_from_gfa(const std::string& gfa_path, const LoadOptions& options) {
    PackedGraph graph;
    graph.load_segments(gfa_path, options);

    std::vector<std::uint32_t> side_counts(graph.node_count() * 2, 0);
    graph.count_links(gfa_path, options, side_counts);

    graph.side_offsets_.assign(side_counts.size() + 1, 0);
    std::uint32_t running_total = 0;
    for (std::size_t i = 0; i < side_counts.size(); ++i) {
        graph.side_offsets_[i] = running_total;
        running_total += side_counts[i];
    }
    graph.side_offsets_[side_counts.size()] = running_total;

    graph.adjacent_sides_.assign(running_total, 0);
    graph.overlaps_.assign(running_total, 0);
    graph.fill_links(gfa_path, options);

    graph.node_visit_marks_.assign(graph.node_count(), 0);
    graph.side_seen_marks_.assign(side_counts.size(), 0);
    graph.side_stack_marks_.assign(side_counts.size(), 0);
    graph.side_processed_marks_.assign(side_counts.size(), 0);

    return graph;
}

void PackedGraph::load_segments(const std::string& gfa_path, const LoadOptions& options) {
    Reader reader(Reader::Options{options.reader_buffer_size, false, options.progress_every});
    if (!reader.open(gfa_path)) {
        throw std::runtime_error("Could not open graph file: " + gfa_path);
    }

    std::string_view line;
    SegmentFields fields;
    while (reader.read_line(line)) {
        if (line.empty() || line.front() != 'S') {
            continue;
        }
        if (!parse_segment_line(line, fields)) {
            throw std::runtime_error("Malformed segment line in " + gfa_path);
        }

        const node_index_t node_index = static_cast<node_index_t>(node_ids_.size());
        const std::string node_id(fields.id);
        if (!id_to_index_.emplace(node_id, node_index).second) {
            throw std::runtime_error("Duplicate node id in graph: " + node_id);
        }

        node_ids_.push_back(node_id);
        const std::uint32_t length = bubblegun::fs::segment_length(fields.sequence, fields.optional_fields);
        node_lengths_.push_back(length);
        total_sequence_length_ += length;

        if (options.store_sequences) {
            sequences_.emplace_back(fields.sequence);
        }
        if (options.store_optional_fields) {
            optional_fields_.emplace_back(fields.optional_fields);
        }
    }
}

void PackedGraph::count_links(const std::string& gfa_path,
                              const LoadOptions& options,
                              std::vector<std::uint32_t>& side_counts) {
    Reader reader(Reader::Options{options.reader_buffer_size, false, options.progress_every});
    if (!reader.open(gfa_path)) {
        throw std::runtime_error("Could not open graph file: " + gfa_path);
    }

    std::string_view line;
    LinkFields fields;
    while (reader.read_line(line)) {
        if (line.empty() || line.front() != 'L') {
            continue;
        }
        if (!parse_link_line(line, fields)) {
            throw std::runtime_error("Malformed link line in " + gfa_path);
        }

        const side_t source_side = parse_link_side(fields.from_id, fields.from_orient, true);
        const side_t target_side = parse_link_side(fields.to_id, fields.to_orient, false);
        ++side_counts[source_side];
        ++side_counts[target_side];
        ++edge_count_;
    }
}

void PackedGraph::fill_links(const std::string& gfa_path, const LoadOptions& options) {
    std::vector<std::uint32_t> cursor(side_offsets_.begin(), side_offsets_.end() - 1);

    Reader reader(Reader::Options{options.reader_buffer_size, false, options.progress_every});
    if (!reader.open(gfa_path)) {
        throw std::runtime_error("Could not open graph file: " + gfa_path);
    }

    std::string_view line;
    LinkFields fields;
    while (reader.read_line(line)) {
        if (line.empty() || line.front() != 'L') {
            continue;
        }
        if (!parse_link_line(line, fields)) {
            throw std::runtime_error("Malformed link line in " + gfa_path);
        }

        const side_t source_side = parse_link_side(fields.from_id, fields.from_orient, true);
        const side_t target_side = parse_link_side(fields.to_id, fields.to_orient, false);

        const std::uint32_t source_offset = cursor[source_side]++;
        adjacent_sides_[source_offset] = target_side;
        overlaps_[source_offset] = fields.overlap;

        const std::uint32_t target_offset = cursor[target_side]++;
        adjacent_sides_[target_offset] = source_side;
        overlaps_[target_offset] = fields.overlap;
    }
}

PackedGraph::side_t PackedGraph::parse_link_side(std::string_view node_id, char orientation, bool is_source) const {
    const auto found = id_to_index_.find(std::string(node_id));
    if (found == id_to_index_.end()) {
        throw std::runtime_error("Link references missing node id: " + std::string(node_id));
    }

    bool is_end = false;
    if (is_source) {
        is_end = (orientation == '+');
    } else {
        is_end = (orientation == '-');
    }
    return make_side(found->second, is_end);
}

std::uint32_t PackedGraph::unique_side_neighbor_count(node_index_t node_index, bool is_end) const {
    const side_t side = make_side(node_index, is_end);
    const auto [start, finish] = side_range(side);
    if (finish <= start) {
        return 0;
    }
    if ((finish - start) == 1) {
        return 1;
    }

    std::vector<node_index_t> neighbors;
    neighbors.reserve(finish - start);
    for (std::uint32_t offset = start; offset < finish; ++offset) {
        neighbors.push_back(side_to_node(adjacent_sides_[offset]));
    }
    std::sort(neighbors.begin(), neighbors.end());
    return static_cast<std::uint32_t>(std::unique(neighbors.begin(), neighbors.end()) - neighbors.begin());
}

std::vector<PackedGraph::node_index_t> PackedGraph::neighbor_pair(node_index_t node_index) const {
    std::vector<node_index_t> neighbors;
    neighbors.reserve(side_degree(node_index, false) + side_degree(node_index, true));

    for_each_edge_on_side(node_index, false, [&](side_t side, std::uint32_t) {
        neighbors.push_back(side_to_node(side));
    });
    for_each_edge_on_side(node_index, true, [&](side_t side, std::uint32_t) {
        neighbors.push_back(side_to_node(side));
    });

    std::sort(neighbors.begin(), neighbors.end());
    return neighbors;
}

bool PackedGraph::has_neighbor(node_index_t node_index, node_index_t other_index) const {
    bool found = false;
    for_each_edge_on_side(node_index, false, [&](side_t side, std::uint32_t) {
        if (side_to_node(side) == other_index) {
            found = true;
        }
    });
    if (found) {
        return true;
    }
    for_each_edge_on_side(node_index, true, [&](side_t side, std::uint32_t) {
        if (side_to_node(side) == other_index) {
            found = true;
        }
    });
    return found;
}

PackedGraph::SearchEpochs PackedGraph::next_search_epochs() {
    auto advance_epoch = [](std::uint32_t& epoch, std::vector<std::uint32_t>& marks) {
        ++epoch;
        if (epoch == 0) {
            std::fill(marks.begin(), marks.end(), 0);
            epoch = 1;
        }
        return epoch;
    };

    return SearchEpochs{
        advance_epoch(node_visit_epoch_, node_visit_marks_),
        advance_epoch(side_seen_epoch_, side_seen_marks_),
        advance_epoch(side_stack_epoch_, side_stack_marks_),
        advance_epoch(side_processed_epoch_, side_processed_marks_),
    };
}

}  // namespace bubblegun::graph
