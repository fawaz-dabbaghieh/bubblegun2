# bubblegun_cpp

`bubblegun_cpp` is a focused C++ rewrite of BubbleGun's `bchains` workflow. The current implementation follows the packed-graph path in the Python codebase and keeps the runtime traversal data in dense arrays rather than a dictionary-based graph.

## Current scope

- Supported subcommand: `bchains`
- Supported options: `--bubble_json`, `--only_simple`, `--only_super`, `--save_memory`, `--chains_gfa`
- Not implemented yet: `--fasta`, `--out_haplos`, and the other BubbleGun subcommands

## Design summary

- The graph loader stores node metadata once, then builds a two-sided packed adjacency structure from GFA `L` lines.
- The public graph API is handle-style and exposes oriented node handles through `get_handle`, `flip`, and `follow_edges`.
- Bubble detection uses the same packed superbubble search strategy as the Python implementation, then chains bubbles by shared endpoints and annotates nested superbubbles for JSON compatibility.

## Library

Bubble detection is also a static library, `bubblegun`, usable without the CLI. `find_bubbles` finds every bubble independently (no chaining) and gives each one its `parent`: the index of the smallest superbubble containing it, or `no_parent` at the top level. Following parents gives the full nesting stack, innermost first.

```cpp
#include "bubblegun.h"

using bubblegun::graph::PackedGraph;

// From a GFA file, or from memory with nodes indexed by position:
PackedGraph graph = PackedGraph::from_edges(
    {"1", "2", "3", "4"}, {10, 1, 1, 10},
    {{0, false, 1, false}, {0, false, 2, false}, {1, false, 3, false}, {2, false, 3, false}});

for (const auto& bubble : bubblegun::bubbles::find_bubbles(graph)) {
    // bubble.type, bubble.source, bubble.sink, bubble.inside, bubble.parent
}
```

`bubblegun::bchains::run` groups the same bubbles into chains, as the `bchains` subcommand does.

CMake options:

- `BUBBLEGUN_BUILD_CLI` (default `ON`): build `bubblegun_cpp`.
- `BUBBLEGUN_WITH_GFA_READER` (default `ON`): `PackedGraph::load_from_gfa`, which needs zlib. With it off the library has no dependencies and only `from_edges` loads graphs, e.g. for WebAssembly.

## Build

The project vendors `argparse` as a git submodule under `external/argparse`.

```bash
git submodule update --init --recursive
cd /workspace/fawaz/bubblegun_cpp/bubblegun2
cmake -S . -B build
cmake --build build -j
```

## Usage

```bash
./build/bubblegun_cpp -g /path/to/graph.gfa --log_file run.log bchains
```

Examples:

```bash
./build/bubblegun_cpp -g ../benchmark_graphs/GCA.cleaned.gfa --log_file GCA.cleaned.log bchains
./build/bubblegun_cpp -g ../benchmark_graphs/GCA.cleaned.gfa bchains --bubble_json GCA.cleaned.json
./build/bubblegun_cpp -g ../benchmark_graphs/GCA.cleaned.gfa bchains --only_simple
./build/bubblegun_cpp -g ../benchmark_graphs/GCA.cleaned.gfa bchains --chains_gfa GCA.cleaned.chains.gfa
```

## Benchmarking

The requested timing binary for local tests is:

```bash
/workspace/fawaz/miniforge/envs/bubblegun_cpp/bin/time -v
```

Example:

```bash
/workspace/fawaz/miniforge/envs/bubblegun_cpp/bin/time -v ./build/bubblegun_cpp -g ../benchmark_graphs/GCA.cleaned.gfa --log_file GCA.cleaned.log bchains
```
