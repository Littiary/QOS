"""QOS pipeline CLI.

Usage:
    python -m qos_core generate --config configs/otuken_v1.yaml
    python -m qos_core train    --config configs/otuken_v1.yaml
    python -m qos_core export   --config configs/otuken_v1.yaml
    python -m qos_core all      --config configs/otuken_v1.yaml
"""

from __future__ import annotations

import argparse
import json
import sys
import time

from qos_core import __version__, datafactory, distill, export
from qos_core.config import load_config


def _cmd_generate(cfg) -> dict:
    t0 = time.perf_counter()
    x, y = datafactory.generate_dataset(cfg)
    datafactory.save_dataset(cfg, x, y)
    elapsed = time.perf_counter() - t0
    info = {
        "n_scenarios": len(x),
        "labeler": cfg.dataset.labeler,
        "label_mean": float(y.mean()),
        "label_std": float(y.std()),
        "seconds": round(elapsed, 2),
        "dataset_path": str(cfg.dataset_path),
    }
    print(f"[generate] {json.dumps(info, indent=2)}")
    return info


def _cmd_train(cfg) -> dict:
    x, y = datafactory.load_dataset(cfg)
    model, metrics = distill.train_student(cfg, x, y)
    distill.save_student(cfg, model)
    summary = {k: v for k, v in metrics.items() if k != "history"}
    print(f"[train] {json.dumps(summary, indent=2)}")
    return metrics


def _cmd_export(cfg) -> dict:
    model = distill.load_student(cfg)
    result = export.export_onnx(cfg, model)
    print(f"[export] {json.dumps(result, indent=2)}")
    if not result["parity_ok"]:
        raise SystemExit("ONNX parity check FAILED - do not deploy this model.")
    return result


def main() -> None:
    # Legacy Windows console codepages (e.g. cp1254) cannot print the
    # circuit diagram's box-drawing characters.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(prog="qos_core", description=__doc__)
    parser.add_argument("command", choices=["generate", "train", "export", "all"])
    parser.add_argument("--config", required=True, help="path to a YAML config")
    args = parser.parse_args()

    cfg = load_config(args.config)
    print(f"QOS core v{__version__} | run: {cfg.name} | labeler: {cfg.dataset.labeler}")

    report: dict = {"name": cfg.name, "config": args.config}
    if args.command in ("generate", "all"):
        report["generate"] = _cmd_generate(cfg)
    if args.command in ("train", "all"):
        metrics = _cmd_train(cfg)
        report["train"] = {k: v for k, v in metrics.items() if k != "history"}
    if args.command in ("export", "all"):
        report["export"] = _cmd_export(cfg)

    cfg.run_dir.mkdir(parents=True, exist_ok=True)
    cfg.report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"report -> {cfg.report_path}")


if __name__ == "__main__":
    main()
