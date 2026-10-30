"""Phase 1 - the Quantum Data Factory (paper section 3).

Generates random scenarios, labels them with the configured labeler
(quantum oracle or a classical baseline) and stores the dataset.
"""

from __future__ import annotations

import numpy as np

from qos_core import heuristic
from qos_core.config import QOSConfig
from qos_core.oracle import StochasticOracle


def generate_dataset(cfg: QOSConfig, progress: bool = True) -> tuple[np.ndarray, np.ndarray]:
    """Returns (x, y): scenarios [n, n_inputs] in [0,1] and labels P_stress [n]."""
    rng = np.random.default_rng(cfg.seed)
    x = rng.uniform(0.0, 1.0, size=(cfg.dataset.n_scenarios, cfg.oracle.n_qubits))

    labeler = cfg.dataset.labeler
    if labeler == "quantum":
        oracle = StochasticOracle(cfg.oracle)
        if progress:
            print(oracle.describe())
        y = oracle.evaluate(x, seed=cfg.seed, progress=progress)
    elif labeler == "threshold":
        y = heuristic.threshold_labels(x)
    elif labeler == "logistic":
        y = heuristic.logistic_labels(x)
    elif labeler == "logistic8":
        y = heuristic.logistic8_labels(x)
    else:  # pragma: no cover - config validation rejects this earlier
        raise ValueError(f"unknown labeler: {labeler}")

    return x.astype(np.float32), y.astype(np.float32)


def save_dataset(cfg: QOSConfig, x: np.ndarray, y: np.ndarray) -> None:
    cfg.run_dir.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        cfg.dataset_path,
        x=x,
        y=y,
        inputs=np.array(cfg.oracle.inputs),
        labeler=cfg.dataset.labeler,
    )


def load_dataset(cfg: QOSConfig) -> tuple[np.ndarray, np.ndarray]:
    data = np.load(cfg.dataset_path, allow_pickle=False)
    return data["x"], data["y"]
