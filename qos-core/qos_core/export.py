"""Phase 3 - ONNX export and runtime parity verification (paper section 4.4).

The exported model is the single deployable artifact: qos-runtime (C++),
Unreal's NNE, or any ONNX-capable host consumes it without Python.
Export is only considered successful when ONNX Runtime reproduces the
PyTorch outputs within tolerance.
"""

from __future__ import annotations

import numpy as np
import torch

from qos_core.config import QOSConfig
from qos_core.distill import StudentNet

_PARITY_TOLERANCE = 1e-4


def export_onnx(cfg: QOSConfig, model: StudentNet) -> dict:
    cfg.run_dir.mkdir(parents=True, exist_ok=True)
    model.eval()
    dummy = torch.zeros(1, cfg.oracle.n_qubits, dtype=torch.float32)

    torch.onnx.export(
        model,
        (dummy,),
        str(cfg.onnx_path),
        input_names=["game_state"],
        output_names=["stress_score"],
        dynamic_axes={"game_state": {0: "batch"}, "stress_score": {0: "batch"}},
        opset_version=cfg.export.opset,
        dynamo=False,
    )
    return verify_parity(cfg, model)


def verify_parity(cfg: QOSConfig, model: StudentNet) -> dict:
    """Compare PyTorch vs ONNX Runtime outputs on random probes."""
    import onnxruntime as ort

    rng = np.random.default_rng(cfg.seed)
    probes = rng.uniform(0, 1, size=(4096, cfg.oracle.n_qubits)).astype(np.float32)

    with torch.no_grad():
        torch_out = model(torch.from_numpy(probes)).numpy()

    session = ort.InferenceSession(
        str(cfg.onnx_path), providers=["CPUExecutionProvider"]
    )
    ort_out = session.run(["stress_score"], {"game_state": probes})[0]

    max_diff = float(np.abs(torch_out - ort_out).max())
    return {
        "onnx_path": str(cfg.onnx_path),
        "opset": cfg.export.opset,
        "parity_max_abs_diff": max_diff,
        "parity_ok": max_diff < _PARITY_TOLERANCE,
    }
