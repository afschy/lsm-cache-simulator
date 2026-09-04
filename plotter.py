#!/usr/bin/env python3
"""Plot metrics of an aggregated .csv as grouped line charts.

The .csv written by logs/aggregate_results.py holds one block per metric,
separated by blank lines:

    disk_reads,<workload>,<workload>,...
    <POLICY>,<value>,<value>,...

One chart is drawn per metric: disk_reads and the miss counts straight from
their blocks (miss_count, filter_miss_count, data_miss_count), plus the matching
miss rates -- miss_rate from its block, and filter_miss_rate / data_miss_rate as
the ratios filter_miss_count/filter_count and data_miss_count/data_count (the
shared count scale cancels in the ratio).  In every chart the workloads run
along the x axis and each policy is one colored line across them.  With no policy
names on the command line every policy in the file is drawn; naming policies
restricts the charts to those, keeping each one's color.  The charts are written
to plots/<csv stem>/<metric>.pdf; a metric whose columns are blank (e.g. the
filter/data metrics for a mode-0 set) is skipped.
"""

import argparse
import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# (name, source) per chart.  A "block" source is read straight from the .csv; a
# "ratio" source divides the first block by the second, cell by cell.
METRICS = [
    ("disk_reads", ("block", "disk_reads")),
    ("miss_count", ("block", "miss_count")),
    ("miss_rate", ("block", "miss_rate")),
    ("filter_miss_count", ("block", "filter_miss_count")),
    ("filter_miss_rate", ("ratio", "filter_miss_count", "filter_count")),
    ("data_miss_count", ("block", "data_miss_count")),
    ("data_miss_rate", ("ratio", "data_miss_count", "data_count")),
]

OUTPUT_DIR = Path(__file__).resolve().parent / "plots"

# Validated categorical palette (light surface), assigned by slot order and
# never cycled; a policy keeps its slot no matter which subset is plotted.
SERIES_COLORS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100",
                 "#e87ba4", "#008300", "#4a3aa7", "#e34948"]

SURFACE = "#fcfcfb"
INK_PRIMARY = "#0b0b0b"
INK_MUTED = "#898781"
GRIDLINE = "#e1e0d9"
BASELINE = "#c3c2b7"


def read_blocks(path):
    """{metric: (workloads, {policy: [values]})} for every block in the .csv."""
    blocks, header = {}, None
    with path.open(newline="") as file:
        for row in csv.reader(file):
            if not row or not row[0]:
                header = None          # blank line ends the current block
                continue
            if header is None:
                header, series = row[0], {}
                blocks[header] = (row[1:], series)
                continue
            series[row[0]] = [float(cell) if cell else None for cell in row[1:]]
    return blocks


def metric_series(blocks, source):
    """(workloads, {policy: [values]}) for one metric source, ratios computed."""
    if source[0] == "block":
        return blocks[source[1]]
    (workloads, num), (_, den) = blocks[source[1]], blocks[source[2]]
    series = {}
    for policy, nums in num.items():
        dens = den.get(policy, [])
        series[policy] = [n / d if n is not None and d else None
                          for n, d in zip(nums, dens)]
    return workloads, series


def has_data(series, policies):
    """True when at least one drawn policy has at least one value."""
    return any(value is not None
               for policy in policies for value in series[policy])


def color_map(blocks):
    """A stable color per policy, in the file's policy order across all blocks."""
    policies = []
    for _workloads, series in blocks.values():
        for policy in series:
            if policy not in policies:
                policies.append(policy)
    return {policy: SERIES_COLORS[index % len(SERIES_COLORS)]
            for index, policy in enumerate(policies)}


def select(series, names):
    """The requested policies in file order, matched case-insensitively."""
    if not names:
        return list(series)
    lookup = {policy.lower(): policy for policy in series}
    chosen, unknown = set(), []
    for name in names:
        policy = lookup.get(name.lower())
        if policy is None:
            unknown.append(name)
        else:
            chosen.add(policy)
    if unknown:
        raise LookupError(", ".join(unknown))
    return [policy for policy in series if policy in chosen]


def plot(workloads, series, policies, colors, metric, title):
    """One line per policy across the workloads, marked at every workload."""
    positions = range(len(workloads))

    figure, axes = plt.subplots(figsize=(max(7.0, 1.4 * len(workloads)), 5.0))
    figure.patch.set_facecolor(SURFACE)
    axes.set_facecolor(SURFACE)

    for policy in policies:
        values = [float("nan") if value is None else value for value in series[policy]]
        axes.plot(list(positions), values, label=policy, color=colors[policy],
                  linewidth=2.0, marker="o", markersize=5.5,
                  markeredgecolor=SURFACE, markeredgewidth=1.0)

    axes.set_xticks(list(positions))
    axes.set_xticklabels(workloads)
    axes.set_ylabel(metric)
    axes.set_xlabel("workload")
    axes.margins(x=0.04)
    axes.set_ylim(bottom=0)

    axes.set_axisbelow(True)
    axes.grid(axis="y", color=GRIDLINE, linewidth=0.8)
    axes.grid(axis="x", visible=False)
    for side in ("top", "right"):
        axes.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        axes.spines[side].set_color(BASELINE)
    axes.tick_params(colors=INK_MUTED, length=0)
    axes.xaxis.label.set_color(INK_MUTED)
    axes.yaxis.label.set_color(INK_MUTED)

    # Legend above the axes, outside the plotting area, with the title above it.
    columns = min(len(policies), 4)
    legend = axes.legend(loc="lower center", bbox_to_anchor=(0.5, 1.02),
                         ncol=columns, frameon=False, handlelength=1.6,
                         columnspacing=1.6)
    for text in legend.get_texts():
        text.set_color(INK_PRIMARY)
    axes.set_title(title, color=INK_PRIMARY, fontsize=12, pad=26 + 18 *
                   ((len(policies) + columns - 1) // columns))
    figure.tight_layout()
    return figure


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, help="aggregated .csv to plot")
    parser.add_argument("policies", nargs="*",
                        help="policies to draw (default: every policy in the file)")
    parser.add_argument("-o", "--output", type=Path,
                        help="output directory (default: plots/<csv stem>/)")
    args = parser.parse_args()

    try:
        blocks = read_blocks(args.csv)
    except OSError as error:
        print(error, file=sys.stderr)
        return 1

    colors = color_map(blocks)
    output_dir = args.output or OUTPUT_DIR / args.csv.stem
    output_dir.mkdir(parents=True, exist_ok=True)

    written = 0
    for metric, source in METRICS:
        try:
            workloads, series = metric_series(blocks, source)
        except KeyError:
            print(f"{args.csv}: no {metric} data", file=sys.stderr)
            continue

        try:
            policies = select(series, args.policies)
        except LookupError as error:
            print(f"{args.csv}: unknown policies: {error}", file=sys.stderr)
            print(f"available: {', '.join(series)}", file=sys.stderr)
            return 1

        if not workloads or not has_data(series, policies):
            print(f"{args.csv}: {metric} is empty, skipped", file=sys.stderr)
            continue

        figure = plot(workloads, series, policies, colors, metric,
                      f"{metric} - {args.csv.stem}")
        output = output_dir / f"{metric}.pdf"
        figure.savefig(output, facecolor=SURFACE)
        plt.close(figure)
        print(f"{output}: {len(policies)} policies, {len(workloads)} workloads")
        written += 1

    if not written:
        print(f"{args.csv}: nothing to plot", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
