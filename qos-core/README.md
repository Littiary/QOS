# qos-core

The offline half of QOS: VQC stochastic oracle, labeled dataset, distilled
student network, deployable ONNX model.

```bash
pip install -e .

python -m qos_core all --config configs/smoke.yaml       # ~1 min end-to-end check
python -m qos_core all --config configs/otuken_v2.yaml   # production run
```

Stages can also run one at a time: `generate`, `train`, `export`.

## Configs

| Config | Purpose |
|---|---|
| `configs/otuken_v2.yaml` | Recommended. Fixed revision of the paper circuit: half range encoding, CRy(pi/3) soft ring, Ry population bias, readout on q2. See [`../paper/errata.md`](../paper/errata.md). |
| `configs/otuken_v1.yaml` | Faithful reproduction of the published paper (CNOT ring, Rz bias). Kept for the record, its surface is degenerate (see errata). |
| `configs/smoke.yaml` | Fast pipeline check on the v2 circuit: exact probabilities, 5k scenarios, 15 epochs. |
| `configs/ablation_logistic.yaml` | Ablation arm C: same student network, classical logistic labels. Differs from v2 in one line. |

Everything is a parameter: qubit count, input names, entanglement topology
(`ring`, `linear`, `full`, `none`, `cry_ring`, `cry_linear`), bias qubit, gate
and angle, shots (an integer for Aer sampling with shot noise, `null` for exact
statevector probabilities), network width and depth, training hyperparameters.

To adapt QOS to your own engine telemetry, copy a config, rename the
`oracle.inputs`, set `n_qubits` to match and rerun `all`. The exported model
keeps the same runtime contract (`game_state` in, `stress_score` out).

## Artifacts

Each run writes to `artifacts/<name>/`:

- `dataset.npz`: scenarios, labels and labeler provenance
- `student.pt`: trained PyTorch weights
- `qos_model.onnx`: the deployable model. Only written if the ONNX Runtime
  parity check against PyTorch passes.
- `report.json`: dataset stats, validation MAE/MSE, parity results
