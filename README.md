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
