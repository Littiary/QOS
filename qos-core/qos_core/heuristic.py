"""Classical baseline labelers for the ablation study.

The scientific claim behind QOS ("the VQC is a better label prior than a
hand-crafted heuristic") is only defensible if the same student network is
also trained on classical labels and compared on real benchmark data.
These labelers exist for exactly that purpose.

Input semantics follow the v2 convention (see configs/otuken_v2.yaml):
columns are [proximity, object_count, vram_usage], all normalized [0,1],
where proximity = 1 - normalized distance (higher = closer = more pressure).
"""

from __future__ import annotations

import numpy as np


def threshold_labels(x: np.ndarray) -> np.ndarray:
    """Deterministic heuristic in the spirit of the whitepaper's Eq. 1.

    Stress fires when the object is close AND the scene is crowded, or when
    VRAM pressure crosses the critical threshold - a typical hand-written rule.
    """
    proximity, objects, vram = x[:, 0], x[:, 1], x[:, 2]
    stress = ((proximity > 0.65) & (objects > 0.6)) | (vram > 0.8)
    return stress.astype(np.float64)


def logistic_labels(x: np.ndarray) -> np.ndarray:
    """Smooth hand-tuned probabilistic heuristic (the strongest classical rival).

    A logistic blend of the three inputs with VRAM weighted highest, mirroring
    the risk-aversion the v2 circuit injects via the Ry bias.
    """
    proximity, objects, vram = x[:, 0], x[:, 1], x[:, 2]
    z = 2.0 * proximity + 1.5 * objects + 3.5 * vram - 3.5
    return 1.0 / (1.0 + np.exp(-z))
