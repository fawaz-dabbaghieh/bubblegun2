#ifndef BUBBLEGUN_CPP_BUBBLEGUN_H
#define BUBBLEGUN_CPP_BUBBLEGUN_H

// The library interface: build a PackedGraph (from a GFA file or from memory)
// and find its bubbles with their nesting, or group them into chains.
//
//     auto graph = bubblegun::graph::PackedGraph::from_edges(ids, lengths, edges);
//     auto bubbles = bubblegun::bubbles::find_bubbles(graph);

#include "bchains/bchains.h"
#include "bubbles/bubbles.h"
#include "graph/packed_graph.h"

#endif  // BUBBLEGUN_CPP_BUBBLEGUN_H
