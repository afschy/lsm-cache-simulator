#!/usr/bin/env python3
"""Aggregate simulator logs into one .md and one .csv per log set.

Layout expected under the root (default: this script's directory):

    <root>/<set>/<trace>/<POLICY>_<config>.log

For every <set> a table is emitted with one column per <trace> directory and
one row per policy, repeated for each of the reported metrics.  Policies that a
trace directory is missing are left blank.
"""

import argparse
import csv
import re
import sys
from pathlib import Path

def _sum(values, *keys):
    """Add the named counters, or None when any of them is missing."""
    numbers = [values.get(key) for key in keys]
    return None if any(n is None for n in numbers) else sum(numbers)


def _miss_rate(values):
    miss, total = values.get("miss_count"), values.get("total_count")
    return None if miss is None or not total else miss / total


# (label, extractor, scaled) triples, in report order.  Extractors take the
# parsed counters of one log and return a number, or None when unavailable;
# miss_rate is a ratio and is the one metric the scale never touches.
METRICS = [
    ("hit_count", lambda v: v.get("hit_count"), True),
    ("miss_count", lambda v: v.get("miss_count"), True),
    ("miss_rate", _miss_rate, False),
    ("extra_read_count", lambda v: v.get("extra_read_count"), True),
    ("disk_reads", lambda v: _sum(v, "miss_count", "extra_read_count"), True),
    # Mode-1 (data_) logs additionally split hits/misses across the filter and
    # data caches; these are blank for mode-0 logs that never report them.
    ("filter_count", lambda v: v.get("filter_count"), True),
    ("filter_hit_count", lambda v: v.get("filter_hit_count"), True),
    ("filter_miss_count", lambda v: v.get("filter_miss_count"), True),
    ("data_count", lambda v: v.get("data_count"), True),
    ("data_hit_count", lambda v: v.get("data_hit_count"), True),
    ("data_miss_count", lambda v: v.get("data_miss_count"), True),
]

def metrics_for(set_name):
    """METRICS for one set; data_ sets sum disk_reads from the filter/data misses."""
    if not set_name.startswith("data_"):
        return METRICS
    return [(label,
             (lambda v: _sum(v, "filter_miss_count", "data_miss_count"))
             if label == "disk_reads" else extract,
             scaled)
            for label, extract, scaled in METRICS]


DEFAULT_SCALE = 1e8
PRECISION = 4

# POLICY_cs<...>_..._bpk<...>.log            (mode 0) -> POLICY
# POLICY_mode1_fcs<...>_dcs<...>_..._bpk<...>.log (mode 1) -> POLICY
POLICY_RE = re.compile(r"^(?P<policy>.+?)_(?:mode\d+_fcs|cs)\d+.*\.log$")


def natural_key(name):
    """Sort key that orders embedded numbers numerically (ep20 before ep100)."""
    return [int(part) if part.isdigit() else part.lower()
            for part in re.split(r"(\d+)", name)]


def parse_log(path):
    """Read the scalar header of a log file; the series lines are skipped."""
    values = {}
    with path.open() as file:
        for line in file:
            key, _, rest = line.partition(" ")
            if key.endswith("_series"):
                break
            try:
                values[key] = int(rest)
            except ValueError:
                continue
    return values


def collect(set_dir):
    """{trace_name: {policy: metrics}} for every log-holding dir in set_dir."""
    traces = {}
    for trace_dir in sorted(set_dir.iterdir(), key=lambda p: natural_key(p.name)):
        if not trace_dir.is_dir():
            continue
        policies = {}
        for log in sorted(trace_dir.glob("*.log")):
            match = POLICY_RE.match(log.name)
            if match:
                policies[match.group("policy")] = parse_log(log)
        if policies:
            traces[trace_dir.name] = policies
    return traces


def cell(values, extract, scaled, scale):
    """Format one metric of one policy, or "" when the data is unavailable."""
    if values is None:
        return ""
    number = extract(values)
    if number is None:
        return ""
    return f"{number / scale if scaled else number:.{PRECISION}f}"


def build_table(traces, policies, label, extract, scaled, scale):
    """Header row plus one row per policy, all cells already formatted."""
    trace_names = list(traces)
    rows = [[label] + trace_names]
    for policy in policies:
        rows.append([policy] + [cell(traces[t].get(policy), extract, scaled, scale)
                                for t in trace_names])
    return rows


def write_markdown(path, set_name, tables, scale):
    lines = [f"# {set_name}", "",
             f"Counts scaled by 1/{scale:g}; miss_rate is a ratio.", ""]
    # A single width shared by every column of every table, so the whole file
    # lines up as one grid.
    columns = len(tables[0][1][0])
    width = max(len(c) for _, rows in tables for row in rows for c in row)

    def render(cells):
        return "| " + " | ".join(c.ljust(width) for c in cells) + " |"

    rule = "|" + "|".join(["-" * (width + 2)] * columns) + "|"

    for label, rows in tables:
        header, *body = rows
        lines.append(f"## {label}")
        lines.append("")
        lines.append(render(header))
        lines.append(rule)
        lines.extend(render(row) for row in body)
        lines.append("")
    path.write_text("\n".join(lines))


def write_csv(path, tables):
    with path.open("w", newline="") as file:
        writer = csv.writer(file)
        for index, (_, rows) in enumerate(tables):
            if index:
                writer.writerow([])
            writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("root", nargs="?", default=Path(__file__).resolve().parent,
                        type=Path, help="directory holding the log sets (default: %(default)s)")
    parser.add_argument("--scale", type=float, default=DEFAULT_SCALE,
                        help="divisor for every count; miss_rate is never scaled "
                             "(default: %(default)g)")
    args = parser.parse_args()

    if args.scale == 0:
        parser.error("--scale must be non-zero")

    set_dirs = [d for d in sorted(args.root.iterdir(), key=lambda p: natural_key(p.name))
                if d.is_dir()]
    if not set_dirs:
        print(f"no subdirectories under {args.root}", file=sys.stderr)
        return 1

    for set_dir in set_dirs:
        traces = collect(set_dir)
        if not traces:
            print(f"skipping {set_dir.name}: no logs found", file=sys.stderr)
            continue

        policies = sorted({p for t in traces.values() for p in t})
        tables = [(label, build_table(traces, policies, label, extract, scaled, args.scale))
                  for label, extract, scaled in metrics_for(set_dir.name)]

        write_markdown(args.root / f"{set_dir.name}.md", set_dir.name, tables, args.scale)
        write_csv(args.root / f"{set_dir.name}.csv", tables)
        print(f"{set_dir.name}: {len(traces)} directories, {len(policies)} policies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
