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

## Build

The project uses the same `argparse` header library that exists in the sibling `gfaidx` repository.

```bash
cd /workspace/fawaz/bubblegun_cpp/bubblegun_cpp
cmake -S . -B build
cmake --build build -j
```

If `argparse.hpp` is not in the default sibling path, point CMake at it explicitly:

```bash
cmake -S . -B build -DARGPARSE_INCLUDE_DIR=/path/to/argparse/include
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
