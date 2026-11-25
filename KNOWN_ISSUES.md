# Known Issues

## `bchains` JSON: chains nested in a dropped chain look top-level

`build_chains` (`src/bchains/bchains.cpp`, the `chain.ends.size() != 2`
checks) drops any chain that doesn't have exactly two ends, e.g. a circular
chain. Its bubbles are not written to the JSON, and since `assign_ids` only
numbers bubbles in kept chains, they keep `id` 0.

A chain nested inside one of those bubbles then gets `parent_sb` 0 in
`assign_parents`, so it is written without `parent_sb` / `parent_chain` and
looks like a top-level chain, although it is nested.

- **Only the CLI / `bchains::run` output is affected.**
  `bubbles::find_bubbles` stores each bubble's parent as its index in the
  returned vector, so the library always has the right nesting.
- **Not seen yet:** on the LPA region (HPRC v2.1 MC CHM13) all 10,326 bubbles
  end up in chains, so nothing was dropped.

Possible fixes:

- write dropped chains too (e.g. a circular chain with empty `ends`), so their
  bubbles get ids and children can point to them;
- or keep the nesting per bubble in the JSON (a `parent_sb` on each bubble), as
  `find_bubbles` already has it.
