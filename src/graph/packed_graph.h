#ifndef BUBBLEGUN_CPP_GRAPH_PACKED_GRAPH_H
#define BUBBLEGUN_CPP_GRAPH_PACKED_GRAPH_H

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bubblegun::graph {

class PackedGraph {
public:
    using node_index_t = std::uint32_t;
    using side_t = std::uint32_t;
    using handle_t = std::uint32_t;

    struct LoadOptions {
        bool store_sequences = false;
        bool store_optional_fields = false;
        std::size_t reader_buffer_size = 64 * 1024;
        std::uint64_t progress_every = 0;
    };

    struct SearchEpochs {
        std::uint32_t node_visit = 0;
        std::uint32_t side_seen = 0;
        std::uint32_t side_stack = 0;
        std::uint32_t side_processed = 0;
    };

    static PackedGraph load_from_gfa(const std::string& gfa_path, const LoadOptions& options);

    [[nodiscard]] std::size_t node_count() const {
        return node_ids_.size();
    }

    [[nodiscard]] bool empty() const {
        return node_ids_.empty();
    }

    [[nodiscard]] bool has_sequences() const {
        return sequences_.size() == node_ids_.size();
    }

    [[nodiscard]] bool has_optional_fields() const {
        return optional_fields_.size() == node_ids_.size();
    }

    [[nodiscard]] handle_t get_handle(node_index_t node_index, bool is_reverse) const {
        return (node_index << 1U) | static_cast<handle_t>(is_reverse);
    }

    [[nodiscard]] handle_t flip(handle_t handle) const {
        return handle ^ 1U;
    }

    [[nodiscard]] node_index_t get_node_index(handle_t handle) const {
        return handle >> 1U;
    }

    [[nodiscard]] bool get_is_reverse(handle_t handle) const {
        return (handle & 1U) != 0;
    }

    [[nodiscard]] side_t make_side(node_index_t node_index, bool is_end) const {
        return (node_index << 1U) | static_cast<side_t>(is_end);
    }

    [[nodiscard]] node_index_t side_to_node(side_t side) const {
        return side >> 1U;
    }

    [[nodiscard]] bool side_is_end(side_t side) const {
        return (side & 1U) != 0;
    }

    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> side_range(side_t side) const {
        return {side_offsets_[side], side_offsets_[side + 1]};
    }

    [[nodiscard]] const std::string& node_id(node_index_t node_index) const {
        return node_ids_[node_index];
    }

    [[nodiscard]] std::uint32_t node_length(node_index_t node_index) const {
        return node_lengths_[node_index];
    }

    [[nodiscard]] std::string_view node_sequence(node_index_t node_index) const {
        if (!has_sequences()) {
            return {};
        }
        return sequences_[node_index];
    }

    [[nodiscard]] std::string_view node_optional_fields(node_index_t node_index) const {
        if (!has_optional_fields()) {
            return {};
        }
        return optional_fields_[node_index];
    }

    [[nodiscard]] std::uint64_t total_sequence_length() const {
        return total_sequence_length_;
    }

    [[nodiscard]] std::uint64_t edge_count() const {
        return edge_count_;
    }

    [[nodiscard]] std::uint32_t side_degree(node_index_t node_index, bool is_end) const {
        const side_t side = make_side(node_index, is_end);
        return side_offsets_[side + 1] - side_offsets_[side];
    }

    template <typename Callback>
    void for_each_edge_on_side(node_index_t node_index, bool is_end, Callback&& callback) const {
        const side_t side = make_side(node_index, is_end);
        const auto [start, finish] = side_range(side);
        for (std::uint32_t offset = start; offset < finish; ++offset) {
            callback(adjacent_sides_[offset], overlaps_[offset]);
        }
    }

    template <typename Callback>
    void follow_edges(handle_t handle, bool go_left, Callback&& callback) const {
        const node_index_t node_index = get_node_index(handle);
        const bool is_reverse = get_is_reverse(handle);
        const bool exit_is_end = go_left ? is_reverse : !is_reverse;

        for_each_edge_on_side(node_index, exit_is_end, [&](side_t target_side, std::uint32_t) {
            const node_index_t target_node = side_to_node(target_side);
            const bool target_reverse = go_left ? !side_is_end(target_side) : side_is_end(target_side);
            callback(get_handle(target_node, target_reverse));
        });
    }

    [[nodiscard]] std::uint32_t unique_side_neighbor_count(node_index_t node_index, bool is_end) const;
    [[nodiscard]] std::vector<node_index_t> neighbor_pair(node_index_t node_index) const;
    [[nodiscard]] bool has_neighbor(node_index_t node_index, node_index_t other_index) const;

    SearchEpochs next_search_epochs();

    std::vector<std::uint32_t>& mutable_node_visit_marks() {
        return node_visit_marks_;
    }

    std::vector<std::uint32_t>& mutable_side_seen_marks() {
        return side_seen_marks_;
    }

    std::vector<std::uint32_t>& mutable_side_stack_marks() {
        return side_stack_marks_;
    }

    std::vector<std::uint32_t>& mutable_side_processed_marks() {
        return side_processed_marks_;
    }

    [[nodiscard]] const std::vector<std::uint32_t>& adjacent_sides() const {
        return adjacent_sides_;
    }

private:
    void load_segments(const std::string& gfa_path, const LoadOptions& options);
    void count_links(const std::string& gfa_path, const LoadOptions& options, std::vector<std::uint32_t>& side_counts);
    void fill_links(const std::string& gfa_path, const LoadOptions& options);
    side_t parse_link_side(std::string_view node_id, char orientation, bool is_source) const;

    std::unordered_map<std::string, node_index_t> id_to_index_;
    std::vector<std::string> node_ids_;
    std::vector<std::uint32_t> node_lengths_;
    std::vector<std::string> sequences_;
    std::vector<std::string> optional_fields_;

    std::vector<std::uint32_t> side_offsets_;
    std::vector<std::uint32_t> adjacent_sides_;
    std::vector<std::uint32_t> overlaps_;

    std::uint64_t total_sequence_length_ = 0;
    std::uint64_t edge_count_ = 0;

    std::vector<std::uint32_t> node_visit_marks_;
    std::vector<std::uint32_t> side_seen_marks_;
    std::vector<std::uint32_t> side_stack_marks_;
    std::vector<std::uint32_t> side_processed_marks_;

    std::uint32_t node_visit_epoch_ = 0;
    std::uint32_t side_seen_epoch_ = 0;
    std::uint32_t side_stack_epoch_ = 0;
    std::uint32_t side_processed_epoch_ = 0;
};

}  // namespace bubblegun::graph

#endif  // BUBBLEGUN_CPP_GRAPH_PACKED_GRAPH_H
