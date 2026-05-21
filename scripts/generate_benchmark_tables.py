#!/usr/bin/env python3

import argparse
from collections import defaultdict
import re
from pathlib import Path


ELAPSED_RE = re.compile(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):\s+([0-9:.]+)")
MAX_RSS_RE = re.compile(r"Maximum resident set size \(kbytes\):\s+([0-9]+)")

DISPLAY_LABELS = {
    "benchmark_graphs/bubblefinder_paper_graphs_results_v1.2.0": "BubbleGun 1.2.0",
    "benchmark_graphs/bubblefinder_paper_graphs_results_v1.2.8": "BubbleGun 1.2.8",
    "benchmark_graphs/cpp_version": "C++",
    "bubblefinder_paper_graphs_results_v1.2.0": "BubbleGun 1.2.0",
    "bubblefinder_paper_graphs_results_v1.2.8": "BubbleGun 1.2.8",
    "cpp_version": "C++",
}
DISPLAY_ORDER = {
    "BubbleGun 1.2.0": 0,
    "BubbleGun 1.2.8": 1,
    "C++": 2,
}
LOG_SUFFIXES = (".output_and_time.log", ".time.log", ".log")


def parse_log_metrics(log_path: Path) -> dict[str, str | int | None] | None:
    text = log_path.read_text(errors="replace")

    elapsed_match = ELAPSED_RE.search(text)
    max_rss_match = MAX_RSS_RE.search(text)

    if elapsed_match is None and max_rss_match is None:
        return None

    elapsed = elapsed_match.group(1) if elapsed_match else None
    max_rss_kb = int(max_rss_match.group(1)) if max_rss_match else None

    return {
        "elapsed": elapsed,
        "max_rss_kb": max_rss_kb,
    }


def graph_name_from_log(log_path: Path) -> str:
    for suffix in LOG_SUFFIXES:
        if log_path.name.endswith(suffix):
            return log_path.name[: -len(suffix)]
    return log_path.stem


def display_label_for_group(group_key: str) -> str:
    return DISPLAY_LABELS.get(group_key, DISPLAY_LABELS.get(Path(group_key).name, group_key))


def group_sort_key(group_key: str) -> tuple[int, str, str]:
    display_label = display_label_for_group(group_key)
    return (DISPLAY_ORDER.get(display_label, len(DISPLAY_ORDER)), display_label.lower(), group_key.lower())


def discover_version_metrics(base_dir: Path) -> dict[str, dict[str, dict[str, str | int | None]]]:
    version_metrics: dict[str, dict[str, dict[str, str | int | None]]] = defaultdict(dict)

    for log_path in sorted(base_dir.rglob("*.log")):
        metrics = parse_log_metrics(log_path)
        if metrics is None:
            continue

        relative_parent = log_path.parent.relative_to(base_dir)
        group_key = relative_parent.as_posix() if relative_parent.parts else "."
        graph_name = graph_name_from_log(log_path)
        version_metrics[group_key][graph_name] = metrics

    return dict(version_metrics)


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
        "base_dir",
        nargs="?",
        default=None,
        help="Base directory to scan recursively for benchmark log files (default: current directory)",
    )
    parser.add_argument(
        "--base-dir",
        dest="base_dir_flag",
        default=None,
        help="Base directory to scan recursively for benchmark log files",
    )
    parser.add_argument("--root", dest="base_dir_flag", default=None, help=argparse.SUPPRESS)
    args = parser.parse_args()

    base_dir_arg = args.base_dir_flag or args.base_dir or "."
    base_dir = Path(base_dir_arg).resolve()

    version_metrics = discover_version_metrics(base_dir)
    if not version_metrics:
        raise SystemExit(f"No benchmark timing logs found under {base_dir}")

    all_graphs = set()
    for metrics_by_graph in version_metrics.values():
        all_graphs.update(metrics_by_graph.keys())

    ordered_graphs = sorted(all_graphs)
    version_groups = sorted(version_metrics, key=group_sort_key)
    version_labels = [display_label_for_group(group_key) for group_key in version_groups]

    elapsed_rows = [["Graph"] + version_labels]
    rss_rows = [["Graph"] + version_labels]

    for graph_name in ordered_graphs:
        elapsed_row = [graph_name]
        rss_row = [graph_name]

        for group_key in version_groups:
            metrics = version_metrics[group_key].get(graph_name, {})
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
