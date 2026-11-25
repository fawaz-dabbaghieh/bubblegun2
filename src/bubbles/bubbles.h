#ifndef BUBBLEGUN_CPP_BUBBLES_BUBBLES_H
#define BUBBLEGUN_CPP_BUBBLES_BUBBLES_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace bubblegun::graph {
class PackedGraph;
}

namespace bubblegun::bubbles {

inline constexpr std::size_t no_parent = std::numeric_limits<std::size_t>::max();

enum class BubbleType {
    Simple,
    Super,
    Insertion,
};

struct Bubble {
    // Node indices in the PackedGraph; graph.node_id() gives their names.
    std::uint32_t source = 0;
    std::uint32_t sink = 0;
    std::vector<std::uint32_t> inside;
    BubbleType type = BubbleType::Super;
    // Index (in the returned vector) of the smallest superbubble whose inside
    // holds this bubble's ends and inside, or no_parent at the top level.
    // Following parents gives the full nesting stack, innermost first.
    std::size_t parent = no_parent;
};

struct DetectOptions {
    bool only_simple = false;
    bool only_super = false;
};

// Finds every bubble independently (no chaining) and fills in the nesting.
std::vector<Bubble> find_bubbles(graph::PackedGraph& graph, const DetectOptions& options = {});

std::string bubble_type_name(BubbleType type);

}  // namespace bubblegun::bubbles

#endif  // BUBBLEGUN_CPP_BUBBLES_BUBBLES_H
