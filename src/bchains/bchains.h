#ifndef BUBBLEGUN_CPP_BCHAINS_BCHAINS_H
#define BUBBLEGUN_CPP_BCHAINS_BCHAINS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bubbles/bubbles.h"

namespace bubblegun::graph {
class PackedGraph;
}

namespace bubblegun::util {
class Logger;
}

namespace bubblegun::bchains {

using bubbles::BubbleType;
using bubbles::bubble_type_name;

// A found bubble plus the chain-numbered ids used in the JSON output.
struct Bubble : bubbles::Bubble {
    std::size_t id = 0;
    std::size_t parent_chain = 0;
    std::size_t parent_sb = 0;
    std::size_t chain_id = 0;
};

struct BubbleChain {
    std::vector<std::size_t> bubble_indices;
    std::vector<std::size_t> sorted_bubble_indices;
    std::vector<std::uint32_t> ends;
    std::size_t id = 0;
    std::size_t parent_chain = 0;
    std::size_t parent_sb = 0;
};

struct BChainStats {
    std::size_t simple_bubbles = 0;
    std::size_t superbubbles = 0;
    std::size_t insertions = 0;
    double sequence_coverage_pct = 0.0;
    double node_coverage_pct = 0.0;
    std::uint64_t longest_chain_sequence_bp = 0;
    std::size_t longest_chain_bubbles = 0;
};

struct Options {
    bool only_simple = false;
    bool only_super = false;
};

struct Result {
    std::vector<Bubble> bubbles;
    std::vector<BubbleChain> chains;
    BChainStats stats;
};

Result run(graph::PackedGraph& graph, const Options& options, util::Logger* logger = nullptr);
void write_json(const graph::PackedGraph& graph, const Result& result, const std::string& output_path);
void write_chain_gfa(const graph::PackedGraph& graph, const Result& result, const std::string& output_path);

}  // namespace bubblegun::bchains

#endif  // BUBBLEGUN_CPP_BCHAINS_BCHAINS_H
