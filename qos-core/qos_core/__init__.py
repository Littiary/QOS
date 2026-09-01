"""QOS - Quantum Optimization System.

Offline pipeline: VQC stochastic oracle -> labeled dataset -> distilled
student network -> ONNX model deployable through qos-runtime in any engine.

Author: Kaan Esrefoglu (Kestro Games)
"""

__version__ = "0.1.0"

from qos_core.config import QOSConfig, load_config

__all__ = ["QOSConfig", "load_config", "__version__"]
