#include "graph/packed_graph.h"

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
    graph.allocate_links(side_counts);
    graph.fill_links(gfa_path, options);
    graph.allocate_search_marks();

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

}  // namespace bubblegun::graph
