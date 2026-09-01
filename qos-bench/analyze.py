#!/usr/bin/env python3
"""qos-bench analyzer: frame-time CSVs -> smoothness metrics comparison.

Engine-agnostic: any CSV with a `frame_time_ms` column participates.

Usage:
    python analyze.py runs/baseline.csv runs/qos.csv
    python analyze.py --labels baseline,qos runs/a.csv runs/b.csv
"""

from __future__ import annotations

import argparse
import csv
import statistics
from pathlib import Path

STUTTER_MS = 33.3
SEVERE_MS = 50.0


def load_frame_times(path: Path) -> list[float]:
    with path.open(newline="", encoding="utf-8-sig") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None or "frame_time_ms" not in reader.fieldnames:
            raise SystemExit(f"{path}: missing required column 'frame_time_ms'")
        times = [float(row["frame_time_ms"]) for row in reader if row["frame_time_ms"]]
    if len(times) < 100:
        raise SystemExit(f"{path}: only {len(times)} frames; need >= 100 for stable stats")
    return times


def percentile(sorted_values: list[float], pct: float) -> float:
    idx = min(len(sorted_values) - 1, max(0, round(pct / 100 * (len(sorted_values) - 1))))
    return sorted_values[idx]


def analyze(times: list[float]) -> dict:
    s = sorted(times)
    p99 = percentile(s, 99.0)
    worst_1pct = s[int(len(s) * 0.99):] or [s[-1]]
    return {
        "frames": len(times),
        "mean_fps": 1000.0 / statistics.fmean(times),
        "low_1pct_fps": 1000.0 / statistics.fmean(worst_1pct),
        "p95_ms": percentile(s, 95.0),
        "p99_ms": p99,
        "std_ms": statistics.pstdev(times),
        "stutters": sum(t > STUTTER_MS for t in times),
        "severe": sum(t > SEVERE_MS for t in times),
    }


COLUMNS = [
    ("arm", "{}", 14),
    ("frames", "{}", 7),
    ("mean_fps", "{:.1f}", 9),
    ("low_1pct_fps", "{:.1f}", 12),
    ("p95_ms", "{:.2f}", 8),
    ("p99_ms", "{:.2f}", 8),
    ("std_ms", "{:.2f}", 8),
    ("stutters", "{}", 8),
    ("severe", "{}", 7),
]


def format_table(rows: list[dict], markdown: bool) -> str:
    header = [name for name, _, _ in COLUMNS]
    body = [
        [fmt.format(row[name]) for name, fmt, _ in COLUMNS]
        for row in rows
    ]
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
    parser.add_argument("csvs", nargs="+", type=Path, help="frame-time CSVs, one per arm")
    parser.add_argument("--labels", help="comma-separated arm names (default: file stems)")
    args = parser.parse_args()

    labels = (
        args.labels.split(",") if args.labels else [p.stem for p in args.csvs]
    )
    if len(labels) != len(args.csvs):
        raise SystemExit("number of labels must match number of CSVs")

    rows = []
    for label, path in zip(labels, args.csvs):
        row = {"arm": label, **analyze(load_frame_times(path))}
        rows.append(row)

    print()
    print(format_table(rows, markdown=False))
    print(f"\nstutter threshold: >{STUTTER_MS} ms | severe: >{SEVERE_MS} ms")

    report = Path(args.csvs[0]).parent / "report.md"
    report.write_text(
        "# qos-bench report\n\n" + format_table(rows, markdown=True) + "\n",
        encoding="utf-8",
    )
    print(f"markdown report -> {report}")


if __name__ == "__main__":
    main()
