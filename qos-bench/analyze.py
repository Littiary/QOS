#!/usr/bin/env python3
"""qos-bench analyzer: frame-time CSVs -> smoothness metrics comparison.

Engine-agnostic: any CSV with a `frame_time_ms` column participates. When the
optional `tier` column is present (written by the Unreal harness), the report
also shows how much quality each arm gave up to get its frame times.

Usage:
    python analyze.py runs/baseline.csv runs/qos.csv
    python analyze.py --labels baseline,qos runs/a.csv runs/b.csv
"""

from __future__ import annotations

import argparse
import csv
import statistics
from pathlib import Path

BUDGET_MS = 16.67
STUTTER_MS = 33.3
SEVERE_MS = 50.0


def load_rows(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8-sig") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None or "frame_time_ms" not in reader.fieldnames:
            raise SystemExit(f"{path}: missing required column 'frame_time_ms'")
        rows = [row for row in reader if row.get("frame_time_ms")]
    if len(rows) < 100:
        raise SystemExit(f"{path}: only {len(rows)} frames; need >= 100 for stable stats")
    return rows


def percentile(sorted_values: list[float], pct: float) -> float:
    idx = min(len(sorted_values) - 1, max(0, round(pct / 100 * (len(sorted_values) - 1))))
    return sorted_values[idx]


def analyze(rows: list[dict]) -> dict:
    times = [float(r["frame_time_ms"]) for r in rows]
    s = sorted(times)
    worst_1pct = s[int(len(s) * 0.99):] or [s[-1]]
    result = {
        "frames": len(times),
        "mean_fps": 1000.0 / statistics.fmean(times),
        "low_1pct_fps": 1000.0 / statistics.fmean(worst_1pct),
        "p95_ms": percentile(s, 95.0),
        "p99_ms": percentile(s, 99.0),
        "std_ms": statistics.pstdev(times),
        "over_budget": sum(t > BUDGET_MS for t in times),
        "stutters": sum(t > STUTTER_MS for t in times),
        "severe": sum(t > SEVERE_MS for t in times),
        "mean_tier": None,
        "tier_changes": None,
    }
    if rows and rows[0].get("tier") not in (None, ""):
        tiers = [int(float(r["tier"])) for r in rows]
        result["mean_tier"] = statistics.fmean(tiers)
        result["tier_changes"] = sum(1 for a, b in zip(tiers, tiers[1:]) if a != b)
    return result


COLUMNS = [
    ("arm", "{}", 14),
    ("frames", "{}", 7),
    ("mean_fps", "{:.1f}", 9),
    ("low_1pct_fps", "{:.1f}", 12),
    ("p95_ms", "{:.2f}", 8),
    ("p99_ms", "{:.2f}", 8),
    ("std_ms", "{:.2f}", 8),
    ("over_budget", "{}", 11),
    ("stutters", "{}", 8),
    ("severe", "{}", 7),
    ("mean_tier", "{:.2f}", 9),
    ("tier_changes", "{}", 12),
]


def fmt(value, pattern: str) -> str:
    return "-" if value is None else pattern.format(value)


def format_table(rows: list[dict], markdown: bool) -> str:
    header = [name for name, _, _ in COLUMNS]
    body = [[fmt(row[name], pattern) for name, pattern, _ in COLUMNS] for row in rows]
    if markdown:
        lines = ["| " + " | ".join(header) + " |",
                 "|" + "|".join("---" for _ in header) + "|"]
        lines += ["| " + " | ".join(cells) + " |" for cells in body]
        return "\n".join(lines)
    widths = [max(w, len(h)) for h, (_, _, w) in zip(header, COLUMNS)]
    lines = ["  ".join(h.ljust(w) for h, w in zip(header, widths))]
    lines += ["  ".join(c.ljust(w) for c, w in zip(cells, widths)) for cells in body]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csvs", nargs="+", type=Path, help="frame-time CSVs, one per arm/run")
    parser.add_argument("--labels", help="comma-separated arm names (default: file stems)")
    args = parser.parse_args()

    labels = args.labels.split(",") if args.labels else [p.stem for p in args.csvs]
    if len(labels) != len(args.csvs):
        raise SystemExit("number of labels must match number of CSVs")

    rows = [{"arm": label, **analyze(load_rows(path))} for label, path in zip(labels, args.csvs)]

    # With several runs per arm (baseline_run1, baseline_run2, ...) add one
    # median row per arm: the number to quote, per the protocol.
    groups: dict[str, list[dict]] = {}
    for row in rows:
        arm = row["arm"].split("_run")[0]
        groups.setdefault(arm, []).append(row)
    if any(len(g) > 1 for g in groups.values()):
        for arm, g in groups.items():
            median = {"arm": f"{arm} (median of {len(g)})"}
            for key in g[0]:
                if key == "arm":
                    continue
                values = [r[key] for r in g if r[key] is not None]
                median[key] = statistics.median(values) if values else None
            rows.append(median)

    print()
    print(format_table(rows, markdown=False))
    print(f"\nover_budget: >{BUDGET_MS} ms (missed 60 FPS) | stutter: >{STUTTER_MS} ms | severe: >{SEVERE_MS} ms | "
          "mean_tier: 0 = full quality, 3 = lowest (lower is better quality)")

    report = Path(args.csvs[0]).parent / "report.md"
    report.write_text("# qos-bench report\n\n" + format_table(rows, markdown=True) + "\n", encoding="utf-8")
    print(f"markdown report -> {report}")


if __name__ == "__main__":
    main()
