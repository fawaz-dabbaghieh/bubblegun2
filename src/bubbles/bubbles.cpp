#include "bubbles/bubbles.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "graph/packed_graph.h"

namespace bubblegun::bubbles {

using bubblegun::graph::PackedGraph;

namespace {

struct BubbleData {
    std::uint32_t sink = 0;
    std::vector<std::uint32_t> inside;
};

struct SearchScratch {
    std::vector<std::uint32_t> nodes_inside;
    std::unordered_set<std::uint32_t> frontier;
};

struct DetectDebugState {
    bool enabled = false;
    std::uint32_t progress_every_nodes = 100000;
    double progress_every_seconds = 30.0;
    double watchdog_seconds = 5.0;
    std::uint64_t watchdog_edge_steps = 1000000;
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_log_time = start_time;
    std::uint64_t total_inside_nodes = 0;
    std::size_t max_inside_size = 0;
    std::size_t max_frontier_size = 0;
    std::size_t max_frontier_buckets = 0;
    std::size_t max_inside_capacity = 0;
};

std::uint64_t bubble_key(std::uint32_t left, std::uint32_t right) {
    const std::uint32_t high = std::max(left, right);
    const std::uint32_t low = std::min(left, right);
    return (static_cast<std::uint64_t>(high) << 32U) | static_cast<std::uint64_t>(low);
}

std::size_t current_rss_kb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) != 0) {
            continue;
        }

        std::istringstream input(line.substr(6));
        std::size_t rss_kb = 0;
        input >> rss_kb;
        return rss_kb;
    }
    return 0;
}

DetectDebugState make_detect_debug_state() {
    DetectDebugState state;
    const char* enabled = std::getenv("BUBBLEGUN_CPP_DEBUG_DETECT");
    if (enabled == nullptr || std::string_view(enabled).empty() || std::string_view(enabled) == "0") {
        return state;
    }

    state.enabled = true;
    if (const char* every_nodes = std::getenv("BUBBLEGUN_CPP_DEBUG_PROGRESS_NODES")) {
        state.progress_every_nodes = static_cast<std::uint32_t>(std::strtoul(every_nodes, nullptr, 10));
    }
    if (const char* every_seconds = std::getenv("BUBBLEGUN_CPP_DEBUG_PROGRESS_SECONDS")) {
        state.progress_every_seconds = std::strtod(every_seconds, nullptr);
    }
    if (const char* watchdog_seconds = std::getenv("BUBBLEGUN_CPP_DEBUG_WATCHDOG_SECONDS")) {
        state.watchdog_seconds = std::strtod(watchdog_seconds, nullptr);
    }
    if (const char* watchdog_steps = std::getenv("BUBBLEGUN_CPP_DEBUG_WATCHDOG_STEPS")) {
        state.watchdog_edge_steps = std::strtoull(watchdog_steps, nullptr, 10);
    }
    state.start_time = std::chrono::steady_clock::now();
    state.last_log_time = state.start_time;
    return state;
}

void maybe_log_detect_progress(DetectDebugState& state,
                               std::uint32_t node_index,
                               std::size_t node_count,
                               std::size_t bubbles_size,
                               const std::unordered_map<std::uint64_t, std::size_t>& bubble_index_by_key,
                               const SearchScratch& scratch,
                               bool force = false) {
    if (!state.enabled) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - state.start_time).count();
    const double since_last = std::chrono::duration<double>(now - state.last_log_time).count();

    if (!force) {
        if (state.progress_every_nodes != 0 && node_index != 0 && (node_index % state.progress_every_nodes) != 0) {
            if (since_last < state.progress_every_seconds) {
                return;
            }
        }
    }

    const double progress_pct = (node_count == 0) ? 100.0 : (100.0 * static_cast<double>(std::min<std::size_t>(node_index, node_count)) / static_cast<double>(node_count));
    std::cerr
        << "[DBG-DETECT] "
        << "nodes=" << node_index << "/" << node_count
        << " (" << std::fixed << std::setprecision(2) << progress_pct << "%) "
        << "elapsed=" << elapsed << "s "
        << "rss_kb=" << current_rss_kb() << ' '
        << "bubbles=" << bubbles_size << ' '
        << "map_buckets=" << bubble_index_by_key.bucket_count() << ' '
        << "total_inside_nodes=" << state.total_inside_nodes << ' '
        << "scratch_inside_size=" << scratch.nodes_inside.size() << ' '
        << "scratch_inside_cap=" << scratch.nodes_inside.capacity() << ' '
        << "scratch_frontier_size=" << scratch.frontier.size() << ' '
        << "scratch_frontier_buckets=" << scratch.frontier.bucket_count() << ' '
        << "max_inside_size=" << state.max_inside_size << ' '
        << "max_inside_cap=" << state.max_inside_capacity << ' '
        << "max_frontier_size=" << state.max_frontier_size << ' '
        << "max_frontier_buckets=" << state.max_frontier_buckets
        << std::endl;
    state.last_log_time = now;
}

std::optional<BubbleData> find_bubble_data(PackedGraph& graph,
                                           std::uint32_t source_index,
                                           bool source_is_end,
                                           SearchScratch& scratch,
                                           DetectDebugState* debug_state = nullptr) {
    auto& node_visit_marks = graph.mutable_node_visit_marks();
    auto& side_seen_marks = graph.mutable_side_seen_marks();
    auto& side_stack_marks = graph.mutable_side_stack_marks();
    auto& side_processed_marks = graph.mutable_side_processed_marks();
    const auto& adjacent_sides = graph.adjacent_sides();

    const auto epochs = graph.next_search_epochs();
    const auto source_side = graph.make_side(source_index, source_is_end);

    scratch.nodes_inside.clear();
    scratch.frontier.clear();
    scratch.frontier.insert(source_side);
    if (debug_state != nullptr) {
        debug_state->max_frontier_size = std::max(debug_state->max_frontier_size, scratch.frontier.size());
        debug_state->max_frontier_buckets = std::max(debug_state->max_frontier_buckets, scratch.frontier.bucket_count());
    }

    std::uint32_t seen_count = 1;
    side_seen_marks[source_side] = epochs.side_seen;
    side_stack_marks[source_side] = epochs.side_stack;
    std::uint64_t edge_scans = 0;
    const auto search_start = std::chrono::steady_clock::now();
    auto next_watchdog_time = search_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                 std::chrono::duration<double>(debug_state == nullptr ? 0.0 : debug_state->watchdog_seconds));
    std::uint64_t next_watchdog_steps = debug_state == nullptr ? 0 : debug_state->watchdog_edge_steps;

    while (!scratch.frontier.empty()) {
        const auto frontier_it = scratch.frontier.begin();
        const std::uint32_t side = *frontier_it;
        scratch.frontier.erase(frontier_it);
        side_stack_marks[side] = 0;
        side_processed_marks[side] = epochs.side_processed;

        const std::uint32_t node_index = graph.side_to_node(side);
        node_visit_marks[node_index] = epochs.node_visit;
        scratch.nodes_inside.push_back(node_index);

        if (side_seen_marks[side] == epochs.side_seen) {
            side_seen_marks[side] = 0;
            --seen_count;
        }

        const auto [start, finish] = graph.side_range(side);
        if (start == finish) {
            break;
        }

        for (std::uint32_t offset = start; offset < finish; ++offset) {
            ++edge_scans;
            const std::uint32_t target_side = adjacent_sides[offset];
            const std::uint32_t child_index = graph.side_to_node(target_side);
            const bool child_side_is_end = graph.side_is_end(target_side);

            if (child_index == source_index) {
                scratch.frontier.clear();
                break;
            }

            const std::uint32_t child_traversal_side = graph.make_side(child_index, !child_side_is_end);
            if (side_processed_marks[child_traversal_side] == epochs.side_processed) {
                continue;
            }
            if (side_seen_marks[child_traversal_side] != epochs.side_seen) {
                side_seen_marks[child_traversal_side] = epochs.side_seen;
                ++seen_count;
            }

            const auto [parent_start, parent_finish] = graph.side_range(target_side);
            bool all_parents_visited = true;
            for (std::uint32_t parent_offset = parent_start; parent_offset < parent_finish; ++parent_offset) {
                const std::uint32_t parent_index = graph.side_to_node(adjacent_sides[parent_offset]);
                if (node_visit_marks[parent_index] != epochs.node_visit) {
                    all_parents_visited = false;
                    break;
                }
            }

            if (all_parents_visited && side_stack_marks[child_traversal_side] != epochs.side_stack) {
                scratch.frontier.insert(child_traversal_side);
                side_stack_marks[child_traversal_side] = epochs.side_stack;
                if (debug_state != nullptr) {
                    debug_state->max_frontier_size = std::max(debug_state->max_frontier_size, scratch.frontier.size());
                    debug_state->max_frontier_buckets = std::max(debug_state->max_frontier_buckets, scratch.frontier.bucket_count());
                }
            }

            if (debug_state != nullptr && debug_state->enabled) {
                const auto now = std::chrono::steady_clock::now();
                const bool watchdog_by_time =
                    debug_state->watchdog_seconds > 0.0 && now >= next_watchdog_time;
                const bool watchdog_by_steps =
                    debug_state->watchdog_edge_steps > 0 && edge_scans >= next_watchdog_steps;
                if (watchdog_by_time || watchdog_by_steps) {
                    const double elapsed = std::chrono::duration<double>(now - search_start).count();
                    std::cerr
                        << "[DBG-WATCHDOG] "
                        << "source=" << source_index
                        << " direction=" << (source_is_end ? 1 : 0)
                        << " elapsed=" << std::fixed << std::setprecision(2) << elapsed << "s "
                        << "rss_kb=" << current_rss_kb() << ' '
                        << "expanded_nodes=" << scratch.nodes_inside.size() << ' '
                        << "frontier_size=" << scratch.frontier.size() << ' '
                        << "frontier_buckets=" << scratch.frontier.bucket_count() << ' '
                        << "seen_count=" << seen_count << ' '
                        << "edge_scans=" << edge_scans << ' '
                        << "current_node=" << node_index
                        << " current_side=" << (graph.side_is_end(side) ? 1 : 0)
                        << std::endl;

                    if (debug_state->watchdog_seconds > 0.0) {
                        next_watchdog_time = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                       std::chrono::duration<double>(debug_state->watchdog_seconds));
                    }
                    if (debug_state->watchdog_edge_steps > 0) {
                        while (edge_scans >= next_watchdog_steps) {
                            next_watchdog_steps += debug_state->watchdog_edge_steps;
                        }
                    }
                }
            }
        }

        if (scratch.frontier.size() == 1 && seen_count == 1) {
            const std::uint32_t sink_side = *scratch.frontier.begin();
            const std::uint32_t sink_index = graph.side_to_node(sink_side);
            scratch.nodes_inside.push_back(sink_index);
            if (scratch.nodes_inside.size() == 2) {
                break;
            }

            BubbleData data;
            data.sink = sink_index;
            data.inside.assign(scratch.nodes_inside.begin() + 1, scratch.nodes_inside.end() - 1);
            if (debug_state != nullptr) {
                debug_state->max_inside_size = std::max(debug_state->max_inside_size, data.inside.size());
                debug_state->max_inside_capacity = std::max(debug_state->max_inside_capacity, scratch.nodes_inside.capacity());
            }
            return data;
        }
    }

    return std::nullopt;
}

BubbleType classify_bubble(const PackedGraph& graph,
                           std::uint32_t source,
                           std::uint32_t sink,
                           const std::vector<std::uint32_t>& inside) {
    if (inside.size() == 2) {
        const auto first = inside[0];
        const auto second = inside[1];
        if (graph.unique_side_neighbor_count(first, false) == 1 &&
            graph.unique_side_neighbor_count(first, true) == 1 &&
            graph.unique_side_neighbor_count(second, false) == 1 &&
            graph.unique_side_neighbor_count(second, true) == 1 &&
            graph.neighbor_pair(first) == graph.neighbor_pair(second) &&
            !graph.has_neighbor(source, sink)) {
            return BubbleType::Simple;
        }
    }

    if (inside.size() == 1) {
        const auto middle = inside[0];
        std::vector<std::uint32_t> expected = {source, sink};
        std::sort(expected.begin(), expected.end());
        if (graph.unique_side_neighbor_count(middle, false) == 1 &&
            graph.unique_side_neighbor_count(middle, true) == 1 &&
            graph.neighbor_pair(middle) == expected) {
            return BubbleType::Insertion;
        }
    }

    return BubbleType::Super;
}

bool keep_bubble(BubbleType type, const DetectOptions& options) {
    if (options.only_simple) {
        return type == BubbleType::Simple;
    }
    if (options.only_super) {
        return type == BubbleType::Super;
    }
    return true;
}

std::vector<Bubble> detect_bubbles(PackedGraph& graph, const DetectOptions& options) {
    std::vector<Bubble> bubbles;
    std::unordered_map<std::uint64_t, std::size_t> bubble_index_by_key;
    SearchScratch scratch;
    DetectDebugState debug_state = make_detect_debug_state();

    for (std::uint32_t node_index = 0; node_index < graph.node_count(); ++node_index) {
        maybe_log_detect_progress(
            debug_state,
            node_index,
            graph.node_count(),
            bubbles.size(),
            bubble_index_by_key,
            scratch);

        for (const bool is_end : {false, true}) {
            auto bubble_data = find_bubble_data(graph, node_index, is_end, scratch, &debug_state);
            if (!bubble_data.has_value()) {
                continue;
            }

            Bubble bubble;
            bubble.source = node_index;
            bubble.sink = bubble_data->sink;
            bubble.inside = std::move(bubble_data->inside);
            bubble.type = classify_bubble(graph, bubble.source, bubble.sink, bubble.inside);

            if (!keep_bubble(bubble.type, options)) {
                continue;
            }

            const std::uint64_t key = bubble_key(bubble.source, bubble.sink);
            const auto found = bubble_index_by_key.find(key);
            if (found == bubble_index_by_key.end()) {
                bubble_index_by_key.emplace(key, bubbles.size());
                debug_state.total_inside_nodes += bubble.inside.size();
                bubbles.push_back(std::move(bubble));
            } else {
                debug_state.total_inside_nodes -= bubbles[found->second].inside.size();
                debug_state.total_inside_nodes += bubble.inside.size();
                bubbles[found->second] = std::move(bubble);
            }
        }
    }

    maybe_log_detect_progress(
        debug_state,
        static_cast<std::uint32_t>(graph.node_count()),
        graph.node_count(),
        bubbles.size(),
        bubble_index_by_key,
        scratch,
        true);

    return bubbles;
}

// Each bubble's parent is the smallest superbubble whose inside holds all of
// the bubble's nodes (its ends and inside), ties going to the earlier one.
void assign_parents(std::vector<Bubble>& bubbles) {
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> node_to_superbubbles;
    for (std::size_t bubble_index = 0; bubble_index < bubbles.size(); ++bubble_index) {
        if (bubbles[bubble_index].type != BubbleType::Super) {
            continue;
        }
        for (const std::uint32_t node_index : bubbles[bubble_index].inside) {
            node_to_superbubbles[node_index].push_back(bubble_index);
        }
    }

    for (std::size_t bubble_index = 0; bubble_index < bubbles.size(); ++bubble_index) {
        Bubble& bubble = bubbles[bubble_index];
        std::vector<std::uint32_t> bubble_nodes;
        bubble_nodes.reserve(bubble.inside.size() + 2);
        bubble_nodes.push_back(bubble.source);
        bubble_nodes.push_back(bubble.sink);
        bubble_nodes.insert(bubble_nodes.end(), bubble.inside.begin(), bubble.inside.end());
        std::sort(bubble_nodes.begin(), bubble_nodes.end());
        bubble_nodes.erase(std::unique(bubble_nodes.begin(), bubble_nodes.end()), bubble_nodes.end());

        std::unordered_map<std::size_t, std::size_t> candidate_counts;
        for (const std::uint32_t node_index : bubble_nodes) {
            const auto found = node_to_superbubbles.find(node_index);
            if (found == node_to_superbubbles.end()) {
                continue;
            }
            for (const std::size_t superbubble_index : found->second) {
                ++candidate_counts[superbubble_index];
            }
        }

        std::size_t best_parent = no_parent;
        for (const auto& [candidate_index, count] : candidate_counts) {
            if (count != bubble_nodes.size() || candidate_index == bubble_index) {
                continue;
            }
            if (best_parent == no_parent ||
                bubbles[candidate_index].inside.size() < bubbles[best_parent].inside.size() ||
                (bubbles[candidate_index].inside.size() == bubbles[best_parent].inside.size() &&
                 candidate_index < best_parent)) {
                best_parent = candidate_index;
            }
        }
        bubble.parent = best_parent;
    }
}

}  // namespace

std::string bubble_type_name(BubbleType type) {
    switch (type) {
        case BubbleType::Simple:
            return "simple";
        case BubbleType::Super:
            return "super";
        case BubbleType::Insertion:
            return "insertion";
    }
    return "super";
}

std::vector<Bubble> find_bubbles(PackedGraph& graph, const DetectOptions& options) {
    std::vector<Bubble> bubbles = detect_bubbles(graph, options);
    assign_parents(bubbles);
    return bubbles;
}

}  // namespace bubblegun::bubbles
