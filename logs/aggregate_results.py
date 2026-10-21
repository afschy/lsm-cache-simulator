#!/usr/bin/env python3
"""Aggregate simulator logs into one .md and one .csv per log set.

Layout expected under the root (default: this script's directory):

    <root>/fcs<F>_dcs<D>_<set>/<trace>/<POLICY>_mode<M>_fcs<F>_dcs<D>_<...>.log

Each set directory is one (filter cache size, data cache size, trace set)
cluster, written as <root>/fcs<F>_dcs<D>_<set>.{md,csv}.  A table is emitted
with one column per <trace> directory and one row per policy, repeated for each
of the reported metrics.  Policies that a trace directory is missing are left
blank; logs whose fcs/dcs differ from their set directory's are skipped.
Counts are divided by --scale, byte metrics are reported in MiB and rates are
left as ratios.  The .csv opens with a "scale,<divisor>" row, then a blank line,
then the tables.
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


def _ratio(num_key, den_key):
    """Extractor for values[num_key] / values[den_key], None when unavailable."""
    def extract(values):
        num, den = values.get(num_key), values.get(den_key)
        return None if num is None or not den else num / den
    return extract


def _get(key):
    return lambda v: v.get(key)


# (label, extractor, kind) triples, in report order.  Extractors take the parsed
# counters of one log and return a number, or None when unavailable.  "count"
# metrics are divided by the scale, "bytes" are reported in MiB, "rate" is untouched.
METRICS = [
    ("hit_count", _get("hit_count"), "count"),
    ("miss_count", _get("miss_count"), "count"),
    ("miss_rate", _ratio("miss_count", "total_count"), "rate"),
    ("extra_read_count", _get("extra_read_count"), "count"),
    ("disk_reads", lambda v: _sum(v, "miss_count", "extra_read_count"), "count"),
    # Mode-1 (filter_data_) logs additionally split hits/misses across the index,
    # filter and data caches; these are blank for mode-0 logs that never report them.
    ("index_count", _get("index_count"), "count"),
    ("index_hit_count", _get("index_hit_count"), "count"),
    ("index_miss_count", _get("index_miss_count"), "count"),
    ("filter_count", _get("filter_count"), "count"),
    ("filter_hit_count", _get("filter_hit_count"), "count"),
    ("filter_miss_count", _get("filter_miss_count"), "count"),
    ("data_count", _get("data_count"), "count"),
    ("data_hit_count", _get("data_hit_count"), "count"),
    ("data_miss_count", _get("data_miss_count"), "count"),
    # Byte counterparts of the above; blank for logs written before byte accounting.
    ("hit_bytes", _get("hit_bytes"), "bytes"),
    ("miss_bytes", _get("miss_bytes"), "bytes"),
    ("miss_byte_rate", _ratio("miss_bytes", "total_bytes"), "rate"),
    ("extra_read_bytes", _get("extra_read_bytes"), "bytes"),
    ("disk_read_bytes", lambda v: _sum(v, "miss_bytes", "extra_read_bytes"), "bytes"),
    ("index_bytes", _get("index_bytes"), "bytes"),
    ("index_hit_bytes", _get("index_hit_bytes"), "bytes"),
    ("index_miss_bytes", _get("index_miss_bytes"), "bytes"),
    ("filter_bytes", _get("filter_bytes"), "bytes"),
    ("filter_hit_bytes", _get("filter_hit_bytes"), "bytes"),
    ("filter_miss_bytes", _get("filter_miss_bytes"), "bytes"),
    ("data_bytes", _get("data_bytes"), "bytes"),
    ("data_hit_bytes", _get("data_hit_bytes"), "bytes"),
    ("data_miss_bytes", _get("data_miss_bytes"), "bytes"),
]

# filter_data_ traces sum their disk reads from the filter/index/data misses.
FILTER_DATA_DISK_READS = {
    "disk_reads": ("filter_miss_count", "index_miss_count", "data_miss_count"),
    "disk_read_bytes": ("filter_miss_bytes", "index_miss_bytes", "data_miss_bytes"),
}


def extractor_for(label, extract, trace_name):
    """The metric's extractor, with disk reads overridden for filter_data_ traces."""
    if label in FILTER_DATA_DISK_READS and trace_name.startswith("filter_data_"):
        return lambda v: _sum(v, *FILTER_DATA_DISK_READS[label])
    return extract


DEFAULT_SCALE = 1e8
MIB = 1 << 20
PRECISION = 4

# POLICY_mode<M>_fcs<F>_dcs<D>_..._bpk<...>.log -> POLICY, F, D
POLICY_RE = re.compile(r"^(?P<policy>.+?)_mode\d+_(?P<sizes>fcs\d+_dcs\d+)_.*\.log$")

# fcs<F>_dcs<D>_<set> -> fcs<F>_dcs<D>
SET_RE = re.compile(r"^(?P<sizes>fcs\d+_dcs\d+)_.+$")


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


def collect(set_dir, sizes):
    """{trace_name: {policy: metrics}} for every log-holding dir in set_dir."""
    traces = {}
    for trace_dir in sorted(set_dir.iterdir(), key=lambda p: natural_key(p.name)):
        if not trace_dir.is_dir():
            continue
        policies = {}
        for log in sorted(trace_dir.glob("*.log")):
            match = POLICY_RE.match(log.name)
            if not match:
                continue
            if match.group("sizes") != sizes:
                print(f"skipping {log}: cache sizes differ from {set_dir.name}",
                      file=sys.stderr)
                continue
            policies[match.group("policy")] = parse_log(log)
        if policies:
            traces[trace_dir.name] = policies
    return traces


def divisor(kind, scale):
    return {"count": scale, "bytes": MIB, "rate": 1}[kind]


def cell(values, extract, div):
    """Format one metric of one policy, or "" when the data is unavailable."""
    if values is None:
        return ""
    number = extract(values)
    if number is None:
        return ""
    return f"{number / div:.{PRECISION}f}"


def build_table(traces, policies, label, extract, div):
    """Header row plus one row per policy, all cells already formatted."""
    trace_names = list(traces)
    rows = [[label] + trace_names]
    for policy in policies:
        rows.append([policy] + [cell(traces[t].get(policy), extractor_for(label, extract, t), div)
                                for t in trace_names])
    return rows


def write_markdown(path, set_name, tables, scale):
    lines = [f"# {set_name}", "",
             f"Counts scaled by 1/{scale:g}; bytes in MiB; rates are ratios.", ""]
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


def write_csv(path, tables, scale):
    with path.open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["scale", f"{scale:.17g}"])
        for _, rows in tables:
            writer.writerow([])
            writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("root", nargs="?", default=Path(__file__).resolve().parent,
                        type=Path, help="directory holding the log sets (default: %(default)s)")
    parser.add_argument("--scale", type=float, default=DEFAULT_SCALE,
                        help="divisor for every count; bytes are in MiB and rates are never scaled "
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
        set_match = SET_RE.match(set_dir.name)
        if not set_match:
            print(f"skipping {set_dir.name}: not named fcs<F>_dcs<D>_<set>", file=sys.stderr)
            continue
        traces = collect(set_dir, set_match.group("sizes"))
        if not traces:
            print(f"skipping {set_dir.name}: no logs found", file=sys.stderr)
            continue

        policies = sorted({p for t in traces.values() for p in t})
        tables = [(label, build_table(traces, policies, label, extract, divisor(kind, args.scale)))
                  for label, extract, kind in METRICS]

        write_markdown(args.root / f"{set_dir.name}.md", set_dir.name, tables, args.scale)
        write_csv(args.root / f"{set_dir.name}.csv", tables, args.scale)
        print(f"{set_dir.name}: {len(traces)} directories, {len(policies)} policies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
