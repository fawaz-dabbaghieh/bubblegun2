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

struct EndpointSlots {
    std::size_t first = npos;
    std::size_t second = npos;
};

void log_progress(util::Logger* logger, const std::string& message) {
    std::cout << '[' << util::get_time() << "] " << message << '\n';
    if (logger != nullptr) {
        logger->info(message);
    }
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

// find_bubbles already gives each bubble its parent; this turns that into the
// chain-numbered ids the JSON uses. A parent outside every chain has id 0 and
// is skipped, as before.
void assign_parents(std::vector<Bubble>& bubbles, std::vector<BubbleChain>& chains) {
    for (Bubble& bubble : bubbles) {
        if (bubble.parent != bubbles::no_parent) {
            bubble.parent_sb = bubbles[bubble.parent].id;
            bubble.parent_chain = bubbles[bubble.parent].chain_id;
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

Result run(PackedGraph& graph, const Options& options, util::Logger* logger) {
    util::Timer total_timer;
    Result result;

    util::Timer detect_timer;
    log_progress(logger, "Finding bubbles...");
    bubbles::DetectOptions detect_options;
    detect_options.only_simple = options.only_simple;
    detect_options.only_super = options.only_super;
    for (bubbles::Bubble& found : bubbles::find_bubbles(graph, detect_options)) {
        Bubble bubble;
        static_cast<bubbles::Bubble&>(bubble) = std::move(found);
        result.bubbles.push_back(std::move(bubble));
    }
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
