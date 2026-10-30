"""Config schema for the QOS offline pipeline.

Everything the whitepaper fixes as a constant is a parameter here, so the
same pipeline can reproduce the paper (configs/otuken_v1.yaml), run quick
smoke tests, or drive ablations (classical labelers instead of the VQC).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import yaml

VALID_ENTANGLEMENTS = ("ring", "linear", "full", "none", "cry_ring", "cry_linear", "cry_star", "cry_ring_star")
VALID_LABELERS = ("quantum", "threshold", "logistic", "logistic8")
VALID_BIAS_GATES = ("rz", "ry", "rx")

# Default CRy entangling angle for the cry_* topologies (pi/3).
DEFAULT_ENTANGLE_ANGLE = 1.0471975511965976


@dataclass
class BiasConfig:
    """Fixed bias rotation on the critical qubit (paper section 3.2).

    NOTE (see paper/errata.md, finding 2): the paper's Rz placed directly
    before a Z-basis measurement is a no-op - phase gates do not change
    measurement statistics. Use gate "ry" for a bias that actually shifts
    the stress probability; "rz" is kept for paper-faithful reproduction.
    """

    qubit: int = 2
    angle: float = 0.60  # radians
    gate: str = "rz"


@dataclass
class OracleConfig:
    """The VQC stochastic oracle (paper sections 2-3)."""

    n_qubits: int = 3
    inputs: list[str] = field(
        default_factory=lambda: ["distance", "object_count", "vram_usage"]
    )
    entanglement: str = "ring"
    # Angle for the cry_* soft-entanglement topologies; ignored by CNOT ones.
    entangle_angle: float = DEFAULT_ENTANGLE_ANGLE
    # Encoding Ry(pi * scale * x). 1.0 = paper (full range, but additive
    # rotations then wrap past |1> - errata finding 3); 0.5 leaves rotation
    # headroom so the decision surface stays monotone.
    encoding_scale: float = 1.0
    bias: BiasConfig | None = field(default_factory=BiasConfig)
    readout_qubit: int = 0
    # Dedicated readout qubit with no input of its own. Every input qubit gets a
    # CRy(star_angle) into it (cry_star / cry_ring_star), so each input has a
    # direct path to the measurement instead of decaying along a chain.
    ancilla: bool = False
    star_angle: float = 0.39
    # 128 = paper fidelity (sampling noise included). None = exact probabilities
    # from the statevector: much faster, deterministic, no Aer needed.
    shots: int | None = 128

    def __post_init__(self) -> None:
        if self.entanglement not in VALID_ENTANGLEMENTS:
            raise ValueError(
                f"entanglement must be one of {VALID_ENTANGLEMENTS}, got {self.entanglement!r}"
            )
        if self.bias is not None and self.bias.gate not in VALID_BIAS_GATES:
            raise ValueError(
                f"bias.gate must be one of {VALID_BIAS_GATES}, got {self.bias.gate!r}"
            )
        if len(self.inputs) != self.n_qubits:
            raise ValueError(
                f"{self.n_qubits} qubits require {self.n_qubits} input names, got {len(self.inputs)}"
            )
        if self.ancilla:
            self.readout_qubit = self.n_qubits  # the ancilla is the last wire
        elif not 0 <= self.readout_qubit < self.n_qubits:
            raise ValueError("readout_qubit out of range")
        if self.entanglement.endswith("star") and not self.ancilla:
            raise ValueError("star topologies need ancilla: true")
        if self.bias is not None and not 0 <= self.bias.qubit < self.n_qubits:
            raise ValueError("bias.qubit out of range")


@dataclass
class DatasetConfig:
    n_scenarios: int = 250_000
    # Which labeler produces P_stress. "quantum" is QOS proper; "threshold"
    # and "logistic" are the classical baselines for the ablation study.
    labeler: str = "quantum"

    def __post_init__(self) -> None:
        if self.labeler not in VALID_LABELERS:
            raise ValueError(f"labeler must be one of {VALID_LABELERS}, got {self.labeler!r}")


@dataclass
class StudentConfig:
    """The distilled classical network (paper section 4)."""

    hidden: list[int] = field(default_factory=lambda: [64, 64])
    epochs: int = 50
    lr: float = 1e-3
    batch_size: int = 512
    val_split: float = 0.1


@dataclass
class ExportConfig:
    opset: int = 17


@dataclass
class QOSConfig:
    name: str = "qos"
    seed: int = 42
    oracle: OracleConfig = field(default_factory=OracleConfig)
    dataset: DatasetConfig = field(default_factory=DatasetConfig)
    student: StudentConfig = field(default_factory=StudentConfig)
    export: ExportConfig = field(default_factory=ExportConfig)
    artifacts_dir: Path = Path("artifacts")

    # Derived paths -------------------------------------------------------
    @property
    def run_dir(self) -> Path:
        return Path(self.artifacts_dir) / self.name

    @property
    def dataset_path(self) -> Path:
        return self.run_dir / "dataset.npz"

    @property
    def student_path(self) -> Path:
        return self.run_dir / "student.pt"

    @property
    def onnx_path(self) -> Path:
        return self.run_dir / "qos_model.onnx"

    @property
    def report_path(self) -> Path:
        return self.run_dir / "report.json"


def load_config(path: str | Path) -> QOSConfig:
    raw = yaml.safe_load(Path(path).read_text(encoding="utf-8")) or {}

    oracle_raw = dict(raw.get("oracle", {}))
    bias_raw = oracle_raw.pop("bias", "default")
    if bias_raw is None:
        bias = None
    elif bias_raw == "default":
        bias = BiasConfig()
    else:
        bias = BiasConfig(**bias_raw)

    return QOSConfig(
        name=raw.get("name", "qos"),
        seed=raw.get("seed", 42),
        oracle=OracleConfig(bias=bias, **oracle_raw),
        dataset=DatasetConfig(**raw.get("dataset", {})),
        student=StudentConfig(**raw.get("student", {})),
        export=ExportConfig(**raw.get("export", {})),
        artifacts_dir=Path(raw.get("artifacts_dir", "artifacts")),
    )
