#!/usr/bin/env python3
"""Plot the disk_reads table of an aggregated .csv as a grouped bar chart.

The .csv written by logs/aggregate_results.py holds one block per metric,
separated by blank lines:

    disk_reads,<workload>,<workload>,...
    <POLICY>,<value>,<value>,...

Only the disk_reads block is plotted: one bar group per workload, one colored
bar per policy.  With no policy names on the command line every policy in the
file is drawn; naming policies restricts the chart to those, keeping each one's
color.  The chart is written to plots/<csv stem>_disk_reads.pdf.
"""

import argparse
import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

METRIC = "disk_reads"

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


def read_metric(path, metric):
    """(workloads, {policy: [values]}) from the named block of the .csv."""
    workloads, series = None, {}
    with path.open(newline="") as file:
        for row in csv.reader(file):
            if not row or not row[0]:
                if workloads is not None:
                    break          # blank line ends the block we came for
                continue
            if workloads is None:
                if row[0] == metric:
                    workloads = row[1:]
                continue
            series[row[0]] = [float(cell) if cell else None for cell in row[1:]]
    if workloads is None:
        raise KeyError(metric)
    return workloads, series


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


def plot(workloads, series, policies, colors, title):
    """Grouped bars: one group per workload, one bar per policy."""
    span = 0.82                    # share of a group's slot covered by bars
    width = span / len(policies)
    offsets = [-span / 2 + width * (index + 0.5) for index in range(len(policies))]
    positions = range(len(workloads))

    figure, axes = plt.subplots(figsize=(max(7.0, 1.7 * len(workloads)), 5.0))
    figure.patch.set_facecolor(SURFACE)
    axes.set_facecolor(SURFACE)

    for policy, offset in zip(policies, offsets):
        values = [0.0 if value is None else value for value in series[policy]]
        axes.bar([position + offset for position in positions], values,
                 width=width * 0.92,   # the remainder is the surface gap
                 color=colors[policy], label=policy, linewidth=0)

    axes.set_xticks(list(positions))
    axes.set_xticklabels(workloads)
    axes.set_ylabel(METRIC)
    axes.set_xlabel("workload")
    axes.margins(x=0.02)

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
                         ncol=columns, frameon=False, handlelength=1.1,
                         handleheight=1.1, columnspacing=1.6)
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
                        help="output file (default: plots/<csv stem>_%s.pdf)" % METRIC)
    args = parser.parse_args()

    try:
        workloads, series = read_metric(args.csv, METRIC)
    except OSError as error:
        print(error, file=sys.stderr)
        return 1
    except KeyError:
        print(f"{args.csv}: no {METRIC} table", file=sys.stderr)
        return 1

    if not series or not workloads:
        print(f"{args.csv}: {METRIC} table is empty", file=sys.stderr)
        return 1

    # Slots follow the file's full policy list, so filtering never repaints.
    colors = {policy: SERIES_COLORS[index % len(SERIES_COLORS)]
              for index, policy in enumerate(series)}

    try:
        policies = select(series, args.policies)
    except LookupError as error:
        print(f"{args.csv}: unknown policies: {error}", file=sys.stderr)
        print(f"available: {', '.join(series)}", file=sys.stderr)
        return 1

    title = f"{METRIC} - {args.csv.stem}"
    figure = plot(workloads, series, policies, colors, title)

    output = args.output or OUTPUT_DIR / f"{args.csv.stem}_{METRIC}.pdf"
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, facecolor=SURFACE)
    plt.close(figure)
    print(f"{output}: {len(policies)} policies, {len(workloads)} workloads")
    return 0


if __name__ == "__main__":
    sys.exit(main())
