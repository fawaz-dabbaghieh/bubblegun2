#include "graph/packed_graph.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace bubblegun::graph {

PackedGraph PackedGraph::from_edges(std::vector<std::string> node_ids,
                                    std::vector<std::uint32_t> node_lengths,
                                    const std::vector<Edge>& edges) {
    if (node_ids.size() != node_lengths.size()) {
        throw std::invalid_argument("from_edges needs one length per node id");
    }

    PackedGraph graph;
    graph.node_ids_ = std::move(node_ids);
    graph.node_lengths_ = std::move(node_lengths);
    for (const std::uint32_t length : graph.node_lengths_) {
        graph.total_sequence_length_ += length;
    }

    // The same sides an L line uses: a '+' source leaves from its end, a '-'
    // target is entered at its end.
    const auto edge_sides = [&](const Edge& edge) {
        if (edge.from >= graph.node_count() || edge.to >= graph.node_count()) {
            throw std::out_of_range("Edge references a node index past the last node");
        }
        return std::pair{graph.make_side(edge.from, !edge.from_reverse), graph.make_side(edge.to, edge.to_reverse)};
    };

    std::vector<std::uint32_t> side_counts(graph.node_count() * 2, 0);
    for (const Edge& edge : edges) {
        const auto [source_side, target_side] = edge_sides(edge);
        ++side_counts[source_side];
        ++side_counts[target_side];
        ++graph.edge_count_;
    }
    graph.allocate_links(side_counts);

    std::vector<std::uint32_t> cursor(graph.side_offsets_.begin(), graph.side_offsets_.end() - 1);
    for (const Edge& edge : edges) {
        const auto [source_side, target_side] = edge_sides(edge);

        const std::uint32_t source_offset = cursor[source_side]++;
        graph.adjacent_sides_[source_offset] = target_side;
        graph.overlaps_[source_offset] = edge.overlap;

        const std::uint32_t target_offset = cursor[target_side]++;
        graph.adjacent_sides_[target_offset] = source_side;
        graph.overlaps_[target_offset] = edge.overlap;
    }

    graph.allocate_search_marks();
    return graph;
}

void PackedGraph::allocate_links(const std::vector<std::uint32_t>& side_counts) {
    side_offsets_.assign(side_counts.size() + 1, 0);
    std::uint32_t running_total = 0;
    for (std::size_t i = 0; i < side_counts.size(); ++i) {
        side_offsets_[i] = running_total;
        running_total += side_counts[i];
    }
    side_offsets_[side_counts.size()] = running_total;

    adjacent_sides_.assign(running_total, 0);
    overlaps_.assign(running_total, 0);
}

void PackedGraph::allocate_search_marks() {
    node_visit_marks_.assign(node_count(), 0);
    side_seen_marks_.assign(node_count() * 2, 0);
    side_stack_marks_.assign(node_count() * 2, 0);
    side_processed_marks_.assign(node_count() * 2, 0);
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
