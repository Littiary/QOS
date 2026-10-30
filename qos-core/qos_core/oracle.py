"""The VQC Stochastic Oracle (paper sections 2-3).

Builds a parametrized quantum circuit once, then evaluates P_stress for
batches of scenarios either exactly (statevector) or by sampling on the
Aer simulator (paper fidelity, includes shot noise).

Circuit topology (defaults reproduce the whitepaper):
  1. Angle encoding      : Ry(pi * x_i) on qubit i           (Eq. 2/5)
  2. Entanglement layer  : CNOTs in ring/linear/full topology (Eq. 3)
  3. Bias injection      : Rz(lambda) on the critical qubit   (Eq. 6)
  4. Readout             : P(|1>) on the readout qubit        (Eq. 7)
"""

from __future__ import annotations

import numpy as np
from qiskit import QuantumCircuit, transpile
from qiskit.circuit import Parameter
from qiskit.quantum_info import Statevector
from tqdm import tqdm

from qos_core.config import OracleConfig

_AER_CHUNK = 2_000  # circuits per Aer run() call


def _entangling_pairs(n: int, topology: str) -> list[tuple[int, int]]:
    base = topology.removeprefix("cry_").removesuffix("_star")
    if base == "star":
        return []
    if base == "none" or n < 2:
        return []
    if base == "linear":
        return [(i, i + 1) for i in range(n - 1)]
    if base == "ring":
        pairs = [(i, (i + 1) % n) for i in range(n)]
        return pairs[:1] if n == 2 else pairs
    if base == "full":
        return [(i, j) for i in range(n) for j in range(i + 1, n)]
    raise ValueError(f"unknown entanglement topology: {topology}")


class StochasticOracle:
    """Parametrized VQC evaluated over batches of normalized input vectors."""

    def __init__(self, cfg: OracleConfig):
        self.cfg = cfg
        self.params = [Parameter(f"x{i}") for i in range(cfg.n_qubits)]
        self.circuit = self._build_circuit(measured=False)
        self.measured_circuit = self._build_circuit(measured=True)

    def _build_circuit(self, measured: bool) -> QuantumCircuit:
        cfg = self.cfg
        n_wires = cfg.n_qubits + (1 if cfg.ancilla else 0)
        qc = QuantumCircuit(n_wires, 1 if measured else 0)
        for i, p in enumerate(self.params):
            qc.ry(np.pi * cfg.encoding_scale * p, i)
        soft = cfg.entanglement.startswith("cry_")
        for control, target in _entangling_pairs(cfg.n_qubits, cfg.entanglement):
            if soft:
                qc.cry(cfg.entangle_angle, control, target)
            else:
                qc.cx(control, target)
        if cfg.bias is not None:
            getattr(qc, cfg.bias.gate)(cfg.bias.angle, cfg.bias.qubit)
        if cfg.entanglement.endswith("star"):
            for i in range(cfg.n_qubits):
                qc.cry(cfg.star_angle, i, cfg.readout_qubit)
        if measured:
            qc.measure(cfg.readout_qubit, 0)
        return qc

    # ------------------------------------------------------------------
    def evaluate(self, x: np.ndarray, seed: int = 0, progress: bool = True) -> np.ndarray:
        """P_stress for each row of x (shape [n, n_qubits], values in [0,1])."""
        x = np.asarray(x, dtype=np.float64)
        if x.ndim != 2 or x.shape[1] != self.cfg.n_qubits:
            raise ValueError(f"expected shape [n, {self.cfg.n_qubits}], got {x.shape}")
        if self.cfg.shots is None:
            return self._evaluate_exact(x, progress)
        return self._evaluate_sampled(x, seed, progress)

    def _evaluate_exact(self, x: np.ndarray, progress: bool) -> np.ndarray:
        probs = np.empty(len(x), dtype=np.float64)
        it = tqdm(x, desc="oracle (exact)", unit="scn", disable=not progress)
        for i, row in enumerate(it):
            bound = self.circuit.assign_parameters(dict(zip(self.params, row)))
            sv = Statevector.from_instruction(bound)
            # marginal probability of the readout qubit being |1>
            marginal = sv.probabilities([self.cfg.readout_qubit])
            probs[i] = marginal[1]
        return probs

    def _evaluate_sampled(self, x: np.ndarray, seed: int, progress: bool) -> np.ndarray:
        from qiskit_aer import AerSimulator

        backend = AerSimulator(seed_simulator=seed)
        template = transpile(self.measured_circuit, backend)
        shots = int(self.cfg.shots)
        probs = np.empty(len(x), dtype=np.float64)

        it = tqdm(
            range(0, len(x), _AER_CHUNK),
            desc=f"oracle ({shots} shots)",
            unit="chunk",
            disable=not progress,
        )
        for start in it:
            rows = x[start : start + _AER_CHUNK]
            bound = [
                template.assign_parameters(dict(zip(self.params, row))) for row in rows
            ]
            result = backend.run(bound, shots=shots).result()
            for j in range(len(rows)):
                counts = result.get_counts(j)
                probs[start + j] = counts.get("1", 0) / shots
        return probs

    def describe(self) -> str:
        cfg = self.cfg
        bias = (
            f"{cfg.bias.gate.capitalize()}({cfg.bias.angle:g}) on q{cfg.bias.qubit}"
            if cfg.bias
            else "none"
        )
        shots = "exact" if cfg.shots is None else f"{cfg.shots} shots"
        star = f" | star angle {cfg.star_angle:g} into ancilla" if cfg.ancilla else ""
        summary = (
            f"VQC: {cfg.n_qubits} qubits | Ry({cfg.encoding_scale:g}*pi*x) encoding | "
            f"{cfg.entanglement} entanglement{star} | bias: {bias} | "
            f"readout: q{cfg.readout_qubit} | {shots}"
        )
        try:
            art = self.circuit.draw(output="text").single_string()
            return f"{summary}\n{art}"
        except Exception:  # drawing is cosmetic; never let it break a run
            return summary
