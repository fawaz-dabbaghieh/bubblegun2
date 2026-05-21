#include "bchains/bchains.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <iostream>

#include "graph/packed_graph.h"
#include "util/Timer.h"
#include "util/logger.h"

namespace bubblegun::bchains {

using bubblegun::graph::PackedGraph;

namespace {

constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

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

struct EndpointSlots {
    std::size_t first = npos;
    std::size_t second = npos;
};

std::uint64_t bubble_key(std::uint32_t left, std::uint32_t right) {
    const std::uint32_t high = std::max(left, right);
    const std::uint32_t low = std::min(left, right);
    return (static_cast<std::uint64_t>(high) << 32U) | static_cast<std::uint64_t>(low);
}

void log_progress(util::Logger* logger, const std::string& message) {
    std::cout << '[' << util::get_time() << "] " << message << '\n';
    if (logger != nullptr) {
        logger->info(message);
    }
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

bool keep_bubble(BubbleType type, const Options& options) {
    if (options.only_simple) {
        return type == BubbleType::Simple;
    }
    if (options.only_super) {
        return type == BubbleType::Super;
    }
    return true;
}

std::vector<Bubble> detect_bubbles(PackedGraph& graph, const Options& options) {
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

std::size_t take_next_available(EndpointSlots& slots, const std::vector<bool>& bubble_available) {
    if (slots.first != npos && !bubble_available[slots.first]) {
        slots.first = npos;
    }
    if (slots.second != npos && !bubble_available[slots.second]) {
        slots.second = npos;
    }

    if (slots.second != npos) {
        const std::size_t bubble_index = slots.second;
        slots.second = npos;
        return bubble_index;
    }
    if (slots.first != npos) {
        const std::size_t bubble_index = slots.first;
        slots.first = npos;
        return bubble_index;
    }
    return npos;
}

bool has_available(EndpointSlots& slots, const std::vector<bool>& bubble_available) {
    if (slots.first != npos && !bubble_available[slots.first]) {
        slots.first = npos;
    }
    if (slots.second != npos && !bubble_available[slots.second]) {
        slots.second = npos;
    }
    return slots.first != npos || slots.second != npos;
}

void find_chain_ends(const std::vector<Bubble>& bubbles, BubbleChain& chain) {
    std::unordered_map<std::uint32_t, std::uint32_t> counts;
    for (const std::size_t bubble_index : chain.bubble_indices) {
        ++counts[bubbles[bubble_index].source];
        ++counts[bubbles[bubble_index].sink];
    }

    chain.ends.clear();
    for (const auto& [node_index, count] : counts) {
        if (count == 1) {
            chain.ends.push_back(node_index);
        }
    }
    std::sort(chain.ends.begin(), chain.ends.end());
}

void sort_chain(const std::vector<Bubble>& bubbles, BubbleChain& chain, util::Logger* logger) {
    chain.sorted_bubble_indices.clear();
    if (chain.ends.size() != 2) {
        return;
    }

    std::unordered_map<std::uint32_t, std::vector<std::size_t>> node_to_bubbles;
    for (const std::size_t bubble_index : chain.bubble_indices) {
        node_to_bubbles[bubbles[bubble_index].source].push_back(bubble_index);
        node_to_bubbles[bubbles[bubble_index].sink].push_back(bubble_index);
    }

    std::unordered_set<std::size_t> seen;
    std::uint32_t current_node = chain.ends.front();
    while (chain.sorted_bubble_indices.size() < chain.bubble_indices.size()) {
        std::size_t next_bubble = npos;
        for (const std::size_t bubble_index : node_to_bubbles[current_node]) {
            if (seen.find(bubble_index) == seen.end()) {
                next_bubble = bubble_index;
                break;
            }
        }

        if (next_bubble == npos) {
            if (logger != nullptr) {
                logger->error("No unvisited bubble found while sorting a chain");
            }
            break;
        }

        chain.sorted_bubble_indices.push_back(next_bubble);
        seen.insert(next_bubble);
        current_node = (bubbles[next_bubble].source == current_node) ? bubbles[next_bubble].sink : bubbles[next_bubble].source;
    }
}

std::vector<BubbleChain> build_chains(const std::vector<Bubble>& bubbles,
                                      std::uint32_t node_count,
                                      util::Logger* logger) {
    std::vector<EndpointSlots> endpoints(node_count);
    for (std::size_t bubble_index = 0; bubble_index < bubbles.size(); ++bubble_index) {
        auto add_endpoint = [&](std::uint32_t node_index) {
            auto& slots = endpoints[node_index];
            if (slots.first == npos) {
                slots.first = bubble_index;
            } else if (slots.second == npos) {
                slots.second = bubble_index;
            } else {
                throw std::runtime_error("A bubble endpoint was attached to more than two chains");
            }
        };
        add_endpoint(bubbles[bubble_index].source);
        add_endpoint(bubbles[bubble_index].sink);
    }

    std::vector<std::uint32_t> starting_nodes;
    starting_nodes.reserve(node_count);
    for (std::uint32_t node_index = 0; node_index < node_count; ++node_index) {
        const std::size_t degree =
            static_cast<std::size_t>(endpoints[node_index].first != npos) +
            static_cast<std::size_t>(endpoints[node_index].second != npos);
        if (degree == 1) {
            starting_nodes.push_back(node_index);
        }
    }

    if (logger != nullptr) {
        log_progress(logger,
                     std::string("Identified ") + std::to_string(starting_nodes.size()) +
                         " chain start nodes.");
    }

    std::vector<bool> bubble_available(bubbles.size(), true);
    auto build_chain = [&](std::uint32_t start_node) {
        BubbleChain chain;
        std::uint32_t current_node = start_node;
        while (true) {
            std::size_t bubble_index = take_next_available(endpoints[current_node], bubble_available);
            if (bubble_index == npos) {
                break;
            }
            if (!bubble_available[bubble_index]) {
                continue;
            }

            bubble_available[bubble_index] = false;
            chain.bubble_indices.push_back(bubble_index);
            current_node = (bubbles[bubble_index].source == current_node) ? bubbles[bubble_index].sink : bubbles[bubble_index].source;
        }
        return chain;
    };

    std::vector<BubbleChain> chains;
    chains.reserve(bubbles.size());

    for (const std::uint32_t node_index : starting_nodes) {
        BubbleChain chain = build_chain(node_index);
        if (chain.bubble_indices.empty()) {
            continue;
        }
        find_chain_ends(bubbles, chain);
        if (chain.ends.size() != 2) {
            continue;
        }
        sort_chain(bubbles, chain, logger);
        chains.push_back(std::move(chain));
    }

    for (std::uint32_t node_index = 0; node_index < node_count; ++node_index) {
        if (!has_available(endpoints[node_index], bubble_available)) {
            continue;
        }
        BubbleChain chain = build_chain(node_index);
        if (chain.bubble_indices.empty()) {
            continue;
        }
        find_chain_ends(bubbles, chain);
        if (chain.ends.size() != 2) {
            continue;
        }
        sort_chain(bubbles, chain, logger);
        chains.push_back(std::move(chain));
    }

    return chains;
}

void assign_ids(std::vector<Bubble>& bubbles, std::vector<BubbleChain>& chains) {
    std::size_t bubble_id = 1;
    for (std::size_t chain_index = 0; chain_index < chains.size(); ++chain_index) {
        BubbleChain& chain = chains[chain_index];
        chain.id = chain_index + 1;
        for (const std::size_t bubble_index : chain.sorted_bubble_indices) {
            Bubble& bubble = bubbles[bubble_index];
            bubble.id = bubble_id++;
            bubble.chain_id = chain.id;
        }
    }
}

void assign_parents(std::vector<Bubble>& bubbles, std::vector<BubbleChain>& chains) {
    std::vector<std::size_t> superbubble_indices;
    superbubble_indices.reserve(bubbles.size());
    std::unordered_map<std::uint32_t, std::vector<std::size_t>> node_to_superbubbles;

    for (std::size_t bubble_index = 0; bubble_index < bubbles.size(); ++bubble_index) {
        if (bubbles[bubble_index].type != BubbleType::Super) {
            continue;
        }
        superbubble_indices.push_back(bubble_index);
        for (const std::uint32_t node_index : bubbles[bubble_index].inside) {
            node_to_superbubbles[node_index].push_back(bubble_index);
        }
    }

    for (Bubble& bubble : bubbles) {
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

        std::size_t best_parent = npos;
        for (const auto& [candidate_index, count] : candidate_counts) {
            if (count != bubble_nodes.size()) {
                continue;
            }
            if (bubble_key(bubbles[candidate_index].source, bubbles[candidate_index].sink) ==
                bubble_key(bubble.source, bubble.sink)) {
                continue;
            }

            if (best_parent == npos ||
                bubbles[candidate_index].inside.size() < bubbles[best_parent].inside.size() ||
                (bubbles[candidate_index].inside.size() == bubbles[best_parent].inside.size() &&
                 bubbles[candidate_index].id < bubbles[best_parent].id)) {
                best_parent = candidate_index;
            }
        }

        if (best_parent != npos) {
            bubble.parent_sb = bubbles[best_parent].id;
            bubble.parent_chain = bubbles[best_parent].chain_id;
        }
    }

    for (BubbleChain& chain : chains) {
        for (const std::size_t bubble_index : chain.bubble_indices) {
            if (bubbles[bubble_index].parent_sb != 0) {
                chain.parent_sb = bubbles[bubble_index].parent_sb;
                chain.parent_chain = bubbles[bubble_index].parent_chain;
                break;
            }
        }
    }
}

std::uint64_t chain_sequence_length(const PackedGraph& graph,
                                    const std::vector<Bubble>& bubbles,
                                    const BubbleChain& chain) {
    std::unordered_set<std::uint32_t> seen_nodes;
    std::uint64_t total = 0;
    for (const std::size_t bubble_index : chain.bubble_indices) {
        const Bubble& bubble = bubbles[bubble_index];
        if (seen_nodes.insert(bubble.source).second) {
            total += graph.node_length(bubble.source);
        }
        if (seen_nodes.insert(bubble.sink).second) {
            total += graph.node_length(bubble.sink);
        }
        for (const std::uint32_t node_index : bubble.inside) {
            if (seen_nodes.insert(node_index).second) {
                total += graph.node_length(node_index);
            }
        }
    }
    return total;
}

BChainStats compute_stats(const PackedGraph& graph, const Result& result) {
    BChainStats stats;
    if (result.chains.empty()) {
        return stats;
    }

    std::vector<std::uint8_t> in_chains(graph.node_count(), 0);
    std::uint64_t chain_sequence_bp = 0;
    std::size_t chain_node_count = 0;

    for (const BubbleChain& chain : result.chains) {
        stats.longest_chain_bubbles = std::max(stats.longest_chain_bubbles, chain.bubble_indices.size());
        stats.longest_chain_sequence_bp = std::max(
            stats.longest_chain_sequence_bp,
            chain_sequence_length(graph, result.bubbles, chain));

        for (const std::size_t bubble_index : chain.bubble_indices) {
            const Bubble& bubble = result.bubbles[bubble_index];
            switch (bubble.type) {
                case BubbleType::Simple:
                    ++stats.simple_bubbles;
                    break;
                case BubbleType::Super:
                    ++stats.superbubbles;
                    break;
                case BubbleType::Insertion:
                    ++stats.insertions;
                    break;
            }

            const auto mark_node = [&](std::uint32_t node_index) {
                if (in_chains[node_index] == 0) {
                    in_chains[node_index] = 1;
                    ++chain_node_count;
                    chain_sequence_bp += graph.node_length(node_index);
                }
            };

            mark_node(bubble.source);
            mark_node(bubble.sink);
            for (const std::uint32_t node_index : bubble.inside) {
                mark_node(node_index);
            }
        }
    }

    if (graph.total_sequence_length() != 0) {
        stats.sequence_coverage_pct =
            (static_cast<double>(chain_sequence_bp) * 100.0) / static_cast<double>(graph.total_sequence_length());
    }
    if (graph.node_count() != 0) {
        stats.node_coverage_pct =
            (static_cast<double>(chain_node_count) * 100.0) / static_cast<double>(graph.node_count());
    }

    return stats;
}

std::string json_escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 4);
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

template <typename Range, typename Formatter>
void write_json_array(std::ostream& out, const Range& range, Formatter formatter) {
    out << '[';
    bool first = true;
    for (const auto& value : range) {
        if (!first) {
            out << ',';
        }
        first = false;
        formatter(value);
    }
    out << ']';
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

Result run(PackedGraph& graph, const Options& options, util::Logger* logger) {
    util::Timer total_timer;
    Result result;

    util::Timer detect_timer;
    log_progress(logger, "Finding bubbles...");
    result.bubbles = detect_bubbles(graph, options);
    log_progress(logger,
                 std::string("Found ") + std::to_string(result.bubbles.size()) +
                     " bubbles in " + util::format_elapsed(detect_timer.elapsed()) + '.');

    util::Timer chain_timer;
    log_progress(logger, "Connecting bubbles into chains...");
    result.chains = build_chains(result.bubbles, static_cast<std::uint32_t>(graph.node_count()), logger);
    log_progress(logger,
                 std::string("Connected bubbles into ") + std::to_string(result.chains.size()) +
                     " chains in " + util::format_elapsed(chain_timer.elapsed()) + '.');

    util::Timer relation_timer;
    log_progress(logger, "Assigning chain ids and nested parent information...");
    assign_ids(result.bubbles, result.chains);
    assign_parents(result.bubbles, result.chains);
    log_progress(logger,
                 std::string("Assigned chain ids and nested parent information in ") +
                     util::format_elapsed(relation_timer.elapsed()) + '.');

    util::Timer stats_timer;
    log_progress(logger, "Computing chain statistics...");
    result.stats = compute_stats(graph, result);
    log_progress(logger,
                 std::string("Computed chain statistics in ") +
                     util::format_elapsed(stats_timer.elapsed()) + '.');
    log_progress(logger,
                 std::string("Done finding chains in ") +
                     util::format_elapsed(total_timer.elapsed()) + '.');

    return result;
}

void write_json(const PackedGraph& graph, const Result& result, const std::string& output_path) {
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open JSON output file: " + output_path);
    }

    out << '{';
    bool first_chain = true;
    for (const BubbleChain& chain : result.chains) {
        if (!first_chain) {
            out << ',';
        }
        first_chain = false;

        out << '\n'
            << "  \"" << chain.id << "\": {"
            << "\"chain_id\":" << chain.id << ','
            << "\"ends\":";
        write_json_array(out, chain.ends, [&](std::uint32_t node_index) {
            out << '"' << json_escape(graph.node_id(node_index)) << '"';
        });
        out << ",\"bubbles\":";
        write_json_array(out, chain.sorted_bubble_indices, [&](std::size_t bubble_index) {
            const Bubble& bubble = result.bubbles[bubble_index];
            out << '{'
                << "\"type\":\"" << bubble_type_name(bubble.type) << "\","
                << "\"id\":" << bubble.id << ','
                << "\"ends\":";
            const std::array<std::uint32_t, 2> ends = {bubble.source, bubble.sink};
            write_json_array(out, ends, [&](std::uint32_t node_index) {
                out << '"' << json_escape(graph.node_id(node_index)) << '"';
            });
            out << ",\"inside\":";
            write_json_array(out, bubble.inside, [&](std::uint32_t node_index) {
                out << '"' << json_escape(graph.node_id(node_index)) << '"';
            });
            out << '}';
        });

        if (chain.parent_sb != 0) {
            out << ",\"parent_sb\":" << chain.parent_sb << ",\"parent_chain\":" << chain.parent_chain;
        }
        out << '}';
    }
    if (!result.chains.empty()) {
        out << '\n';
    }
    out << "}\n";
}

void write_chain_gfa(const PackedGraph& graph, const Result& result, const std::string& output_path) {
    if (!graph.has_sequences()) {
        throw std::runtime_error("Chain GFA output requires stored node sequences");
    }

    std::vector<std::uint8_t> included(graph.node_count(), 0);
    std::vector<std::uint32_t> node_order;
    for (const BubbleChain& chain : result.chains) {
        for (const std::size_t bubble_index : chain.bubble_indices) {
            const Bubble& bubble = result.bubbles[bubble_index];
            const auto include = [&](std::uint32_t node_index) {
                if (included[node_index] == 0) {
                    included[node_index] = 1;
                    node_order.push_back(node_index);
                }
            };
            include(bubble.source);
            include(bubble.sink);
            for (const std::uint32_t node_index : bubble.inside) {
                include(node_index);
            }
        }
    }

    std::sort(node_order.begin(), node_order.end());

    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("Could not open chain GFA output file: " + output_path);
    }

    for (const std::uint32_t node_index : node_order) {
        const std::string_view sequence = graph.node_sequence(node_index);
        const std::string_view optional_fields = graph.node_optional_fields(node_index);

        out << "S\t" << graph.node_id(node_index) << '\t' << sequence;
        if (optional_fields.find("LN") == std::string_view::npos) {
            out << "\tLN:i:" << graph.node_length(node_index);
        }
        if (!optional_fields.empty()) {
            out << '\t' << optional_fields;
        }
        out << '\n';

        for (const bool is_end : {false, true}) {
            const char from_sign = is_end ? '+' : '-';
            graph.for_each_edge_on_side(node_index, is_end, [&](PackedGraph::side_t target_side, std::uint32_t overlap) {
                const std::uint32_t target_node = graph.side_to_node(target_side);
                if (included[target_node] == 0) {
                    return;
                }
                const char to_sign = graph.side_is_end(target_side) ? '-' : '+';
                out << "L\t"
                    << graph.node_id(node_index)
                    << '\t'
                    << from_sign
                    << '\t'
                    << graph.node_id(target_node)
                    << '\t'
                    << to_sign
                    << '\t'
                    << overlap
                    << "M\n";
            });
        }
    }
}

}  // namespace bubblegun::bchains
