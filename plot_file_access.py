#!/usr/bin/env python3
"""Plot per-file access counts from a bin/analyze_trace .csv.

Writes <csv stem>_total.pdf and <csv stem>_empty.pdf to the current directory.
Files run along the x axis in the .csv's order (level, then file ID), and a
dashed vertical line separates consecutive levels.  On a linear axis the y
range is capped at twice the tallest bar outside level 0 (rounded up to a nice
number), so level 0 doesn't flatten the rest; clipped bars stop at the cap,
whose tick reads "<cap>+".  --skip x drops levels 0 through x; the plots then
get a _skip<x> suffix.
"""

import argparse
import csv
import math
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, MaxNLocator

BAR_COLOR = "#2a78d6"
INK = "#0b0b0b"
MUTED_INK = "#52514e"
GRID = "#e1e0d9"


def read_csv(path):
    levels, totals, empties = [], [], []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            levels.append(int(row["level"]))
            totals.append(int(row["total_accesses"]))
            empties.append(int(row["empty_accesses"]))
    return levels, totals, empties


def nice_ceil(v):
    """Rounds v up to 1, 2, 2.5 or 5 times a power of ten."""
    mag = 10 ** math.floor(math.log10(v))
    return next(step * mag for step in (1, 2, 2.5, 5, 10) if step * mag >= v)


def y_cap(levels, counts):
    """Twice the tallest bar outside level 0, or None if nothing exceeds it."""
    rest = [c for lvl, c in zip(levels, counts) if lvl != 0]
    if not rest or max(rest) == 0:
        return None
    cap = nice_ceil(2 * max(rest))
    return cap if max(counts) > cap else None


def plot(levels, counts, title, ylabel, out_path, log_scale):
    fig, ax = plt.subplots(figsize=(12, 4.5))
    x = range(len(counts))
    cap = None if log_scale else y_cap(levels, counts)
    heights = counts if cap is None else [min(c, cap) for c in counts]
    ax.bar(x, heights, width=1.0, color=BAR_COLOR, linewidth=0)

    # boundaries sit between the last file of one level and the first of the next
    starts = [0] + [i for i in range(1, len(levels)) if levels[i] != levels[i - 1]]
    ends = starts[1:] + [len(levels)]
    for b in starts[1:]:
        ax.axvline(b - 0.5, color=MUTED_INK, linestyle="--", linewidth=1)
    # labels too close to the previous one move up a row so they don't overlap
    prev_center, row = None, 0
    for s, e in zip(starts, ends):
        center = (s + e - 1) / 2
        close = prev_center is not None and (center - prev_center) / len(levels) < 0.03
        row = row + 1 if close else 0
        ax.text(center, 1.01 + 0.05 * row, f"L{levels[s]}", transform=ax.get_xaxis_transform(),
                ha="center", va="bottom", color=MUTED_INK, fontsize=9)
        prev_center = center

    ax.set_xlim(-0.5, len(counts) - 0.5)
    if log_scale:
        ax.set_yscale("log")
    elif cap is None:
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:,.0f}"))
    else:
        ticks = [t for t in MaxNLocator(nbins=5, steps=[1, 2, 2.5, 5, 10]).tick_values(0, cap) if 0 <= t <= cap]
        if ticks[-1] != cap:
            ticks.append(cap)
        ax.set_ylim(0, cap)
        ax.set_yticks(ticks)
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:,.0f}" + ("+" if v >= cap else "")))
    ax.set_xlabel("SST file (sorted by level, then file ID)", color=INK)
    ax.set_ylabel(ylabel, color=INK)
    ax.set_title(title, color=INK, pad=28)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.tick_params(colors=MUTED_INK)

    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)
    print(f"Wrote {out_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", help="CSV written by bin/analyze_trace")
    parser.add_argument("--log", action="store_true", help="log-scale y axis")
    parser.add_argument("--skip", type=int, metavar="X", help="skip levels 0 through X")
    args = parser.parse_args()

    levels, totals, empties = read_csv(args.csv)
    suffix = ""
    if args.skip is not None:
        kept = [i for i, lvl in enumerate(levels) if lvl > args.skip]
        levels, totals, empties = ([col[i] for i in kept] for col in (levels, totals, empties))
        suffix = f"_skip{args.skip}"
    if not levels:
        sys.exit(f"{args.csv} has no rows to plot")

    stem = Path(args.csv).stem
    plot(levels, totals, f"Total queries per file ({stem})", "Total queries",
         f"{stem}_total{suffix}.pdf", args.log)
    plot(levels, empties, f"Empty queries per file ({stem})", "Empty queries",
         f"{stem}_empty{suffix}.pdf", args.log)


if __name__ == "__main__":
    main()
