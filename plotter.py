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
along the x axis and each policy is one line across them.  Policies are split
into a base and a variant prefix (OPTIMIZED_MODULAR_LRU -> LRU, OPTIMIZED_MODULAR_):
the base picks the hue and marker, the variant the shade and dash, so no two
policies share a color.  With no policy names on the command line every policy
in the file is drawn; naming policies restricts the charts to those, keeping each
one's style.  Workloads are grouped by trace name, the word before the first
underscore once the runner's mode prefix is dropped (filter_data_uniform_ep0 ->
uniform), and each group is written to plots/<csv stem>/<trace name>/<metric>.pdf;
a metric whose columns are blank (e.g. the filter/data metrics for a mode-0 set)
is skipped.
"""

import argparse
import csv
import math
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

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

# Mode prefixes run_all_traces.sh puts before each trace name, longest first.
MODE_PREFIXES = ("filter_data_", "filter_")

# One hue per base policy, in order; the first four are the set whose shades
# stay furthest apart across families.  Families past these get generated hues.
FAMILY_COLORS = ["#2a78d6", "#eda100", "#e87ba4", "#008300",
                 "#eb6834", "#4a3aa7", "#1baf7a", "#e34948"]
FAMILY_MARKERS = ["o", "s", "^", "D", "v", "P", "X", "h"]
# One OKLCH lightness and dash per variant, darkest/solid for the base policy.
VARIANT_LIGHTNESS = (0.48, 0.78)
VARIANT_DASHES = ["solid", (0, (5, 2)), (0, (1, 1.5)), (0, (6, 2, 1, 2))]

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


def strip_mode_prefix(workload):
    """The workload's trace name, without the runner's mode prefix."""
    for prefix in MODE_PREFIXES:
        if workload.startswith(prefix):
            return workload[len(prefix):]
    return workload


def group_by_trace(workloads, series):
    """{trace name: (unprefixed workloads, {policy: [values]})}, in workload order.

    The trace name is the word before the first underscore of each workload."""
    labels = [strip_mode_prefix(workload) for workload in workloads]
    groups = {}
    for index, label in enumerate(labels):
        groups.setdefault(label.split("_", 1)[0], []).append(index)
    return {name: ([labels[i] for i in indices],
                   {policy: [values[i] for i in indices]
                    for policy, values in series.items()})
            for name, indices in groups.items()}


def has_data(series, policies):
    """True when at least one drawn policy has at least one value."""
    return any(value is not None
               for policy in policies for value in series[policy])


def _to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _to_srgb(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def hex_to_oklch(color):
    r, g, b = (_to_linear(int(color[i:i + 2], 16) / 255) for i in (1, 3, 5))
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            math.hypot(a, b), math.atan2(b, a))


def oklch_to_hex(lightness, chroma, hue):
    """sRGB hex of an OKLCH color, chroma reduced until it fits the gamut."""
    while True:
        a, b = chroma * math.cos(hue), chroma * math.sin(hue)
        l = (lightness + 0.3963377774 * a + 0.2158037573 * b) ** 3
        m = (lightness - 0.1055613458 * a - 0.0638541728 * b) ** 3
        s = (lightness - 0.0894841775 * a - 1.2914855480 * b) ** 3
        rgb = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
               -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
               -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)
        if all(-1e-4 <= c <= 1 + 1e-4 for c in rgb) or chroma < 1e-3:
            break
        chroma *= 0.97
    return "#" + "".join(f"{round(min(max(_to_srgb(c), 0), 1) * 255):02x}" for c in rgb)


def split_policy(policy, names):
    """(base, variant prefix); the base is the shortest name the policy ends with."""
    base = min((name for name in names
                if policy == name or policy.endswith("_" + name)), key=len)
    return base, policy[:len(policy) - len(base)]


def style_map(blocks):
    """{policy: (family, variant, color, marker, dash)}, stable across subsets.

    Families and variants are indexed over every policy in the file, so a
    policy's style never depends on which others are drawn."""
    policies = []
    for _workloads, series in blocks.values():
        for policy in series:
            if policy not in policies:
                policies.append(policy)

    splits = {policy: split_policy(policy, policies) for policy in policies}
    families = list(dict.fromkeys(base for base, _ in splits.values()))
    variants = sorted({variant for _, variant in splits.values()},
                      key=lambda v: (len(v), v))

    family_hues = []
    for index in range(len(families)):
        if index < len(FAMILY_COLORS):
            family_hues.append(hex_to_oklch(FAMILY_COLORS[index]))
        else:   # golden-angle steps keep generated hues apart
            family_hues.append((0.63, 0.14, math.radians(20 + 137.5 * index)))

    low, high = VARIANT_LIGHTNESS
    if len(variants) > 3:
        low, high = 0.36, 0.84
    shades = [low + (high - low) * i / max(len(variants) - 1, 1)
              for i in range(len(variants))]

    styles = {}
    for policy, (base, variant) in splits.items():
        f, v = families.index(base), variants.index(variant)
        hue = family_hues[f]
        color = (FAMILY_COLORS[f] if len(variants) == 1 and f < len(FAMILY_COLORS)
                 else oklch_to_hex(shades[v], hue[1], hue[2]))
        styles[policy] = (f, v, color, FAMILY_MARKERS[f % len(FAMILY_MARKERS)],
                          VARIANT_DASHES[v % len(VARIANT_DASHES)])
    return styles


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


def legend_grid(policies, styles):
    """(handles, labels, columns): a family-per-row, variant-per-column grid
    padded with blanks, or a plain 4-column list when that would not fit."""
    handles = {policy: Line2D([], [], color=styles[policy][2], marker=styles[policy][3],
                              linestyle=styles[policy][4], linewidth=2.0, markersize=6.5,
                              markeredgecolor=SURFACE, markeredgewidth=1.0)
               for policy in policies}
    families = sorted({styles[p][0] for p in policies})
    variants = sorted({styles[p][1] for p in policies})
    if len(variants) == 1 or len(variants) > 4:
        return ([handles[p] for p in policies], list(policies), min(len(policies), 4))

    # Matplotlib fills legends column by column, so emit variant-major order.
    cell = {(styles[p][0], styles[p][1]): p for p in policies}
    blank = Line2D([], [], alpha=0)
    ordered = [cell.get((f, v)) for v in variants for f in families]
    return ([handles[p] if p else blank for p in ordered],
            [p or "" for p in ordered], len(variants))


def plot(workloads, series, policies, styles, metric, title):
    """One line per policy across the workloads, marked at every workload."""
    positions = range(len(workloads))

    figure, axes = plt.subplots(figsize=(max(8.0, 1.7 * len(workloads)), 6.0))
    figure.patch.set_facecolor(SURFACE)
    axes.set_facecolor(SURFACE)

    for policy in policies:
        _f, _v, color, marker, dash = styles[policy]
        values = [float("nan") if value is None else value for value in series[policy]]
        axes.plot(list(positions), values, color=color, linestyle=dash,
                  linewidth=2.0, marker=marker, markersize=6.5,
                  markeredgecolor=SURFACE, markeredgewidth=1.0)

    axes.set_xticks(list(positions))
    axes.set_xticklabels(workloads, rotation=30, ha="right", rotation_mode="anchor")
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
    handles, labels, columns = legend_grid(policies, styles)
    legend = axes.legend(handles, labels, loc="lower center",
                         bbox_to_anchor=(0.5, 1.02), ncol=columns, frameon=False,
                         handlelength=3.2, columnspacing=1.6)
    for text in legend.get_texts():
        text.set_color(INK_PRIMARY)
    axes.set_title(title, color=INK_PRIMARY, fontsize=12, pad=26 + 18 *
                   ((len(labels) + columns - 1) // columns))
    figure.tight_layout()
    return figure


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, help="aggregated .csv to plot")
    parser.add_argument("policies", nargs="*",
                        help="policies to draw (default: every policy in the file)")
    parser.add_argument("-o", "--output", type=Path,
                        help="output directory, one subdirectory per trace name "
                             "(default: plots/<csv stem>/)")
    args = parser.parse_args()

    try:
        blocks = read_blocks(args.csv)
    except OSError as error:
        print(error, file=sys.stderr)
        return 1

    styles = style_map(blocks)
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

        for name, (trace_workloads, trace_series) in group_by_trace(workloads,
                                                                    series).items():
            if not has_data(trace_series, policies):
                print(f"{args.csv}: {name} {metric} is empty, skipped", file=sys.stderr)
                continue

            figure = plot(trace_workloads, trace_series, policies, styles, metric,
                          f"{metric} - {name} - {args.csv.stem}")
            trace_dir = output_dir / name
            trace_dir.mkdir(exist_ok=True)
            output = trace_dir / f"{metric}.pdf"
            figure.savefig(output, facecolor=SURFACE, bbox_inches="tight")
            plt.close(figure)
            print(f"{output}: {len(policies)} policies, {len(trace_workloads)} workloads")
            written += 1

    if not written:
        print(f"{args.csv}: nothing to plot", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
