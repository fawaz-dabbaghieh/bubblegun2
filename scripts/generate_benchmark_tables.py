#!/usr/bin/env python3

import argparse
import re
from pathlib import Path


ELAPSED_RE = re.compile(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):\s+([0-9:.]+)")
MAX_RSS_RE = re.compile(r"Maximum resident set size \(kbytes\):\s+([0-9]+)")


DEFAULT_VERSION_DIRS = {
    "BubbleGun 1.2.0": "benchmark_graphs/bubblefinder_paper_graphs_results_v1.2.0",
    "BubbleGun 1.2.8": "benchmark_graphs/bubblefinder_paper_graphs_results_v1.2.8",
    "C++": "benchmark_graphs/cpp_version",
}


def parse_log_metrics(log_path: Path) -> dict[str, str | int | None]:
    text = log_path.read_text()

    elapsed_match = ELAPSED_RE.search(text)
    max_rss_match = MAX_RSS_RE.search(text)

    elapsed = elapsed_match.group(1) if elapsed_match else None
    max_rss_kb = int(max_rss_match.group(1)) if max_rss_match else None

    return {
        "elapsed": elapsed,
        "max_rss_kb": max_rss_kb,
    }


def collect_version_metrics(version_dir: Path) -> dict[str, dict[str, str | int | None]]:
    metrics_by_graph = {}

    for log_path in sorted(version_dir.glob("*.output_and_time.log")):
        graph_name = log_path.name.removesuffix(".output_and_time.log")
        metrics_by_graph[graph_name] = parse_log_metrics(log_path)

    return metrics_by_graph


def format_rss_kb(max_rss_kb: int | None) -> str:
    if max_rss_kb is None:
        return "-"
    gib = max_rss_kb / (1024 * 1024)
    return f"{gib:.2f} GiB"


def make_markdown_table(title: str, rows: list[list[str]]) -> str:
    header = rows[0]
    separators = ["---"] * len(header)

    table_lines = [title, "", "| " + " | ".join(header) + " |", "| " + " | ".join(separators) + " |"]
    for row in rows[1:]:
        table_lines.append("| " + " | ".join(row) + " |")

    return "\n".join(table_lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate Markdown tables for elapsed time and max RSS from benchmark log files."
    )
    parser.add_argument(
        "--root",
        default=".",
        help="Repository root that contains benchmark_graphs/ (default: current directory)",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()

    version_metrics = {}
    all_graphs = set()

    for label, relative_dir in DEFAULT_VERSION_DIRS.items():
        version_dir = root / relative_dir
        version_metrics[label] = collect_version_metrics(version_dir)
        all_graphs.update(version_metrics[label].keys())

    ordered_graphs = sorted(all_graphs)
    version_labels = list(DEFAULT_VERSION_DIRS.keys())

    elapsed_rows = [["Graph"] + version_labels]
    rss_rows = [["Graph"] + version_labels]

    for graph_name in ordered_graphs:
        elapsed_row = [graph_name]
        rss_row = [graph_name]

        for label in version_labels:
            metrics = version_metrics[label].get(graph_name, {})
            elapsed_row.append(metrics.get("elapsed", "-") or "-")
            rss_row.append(format_rss_kb(metrics.get("max_rss_kb")))

        elapsed_rows.append(elapsed_row)
        rss_rows.append(rss_row)

    print(make_markdown_table("## Elapsed Time", elapsed_rows))
    print()
    print(make_markdown_table("## Max RSS", rss_rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
