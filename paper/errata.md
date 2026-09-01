# Errata and Circuit Revision Notes

Findings from implementing the whitepaper's circuit exactly as specified
(`configs/otuken_v1.yaml`) and probing its decision surface. Found on
2026-09-01 during the open source implementation, verified numerically with
exact statevector simulation. These findings motivate the v2 circuit
(`configs/otuken_v2.yaml`).

## Finding 1: the readout is independent of Distance

With the paper's topology (Ry(pi*x_i) encodings, CNOT ring `CX(0,1), CX(1,2),
CX(2,0)`, measure q0), the CNOTs act classically on the computational basis
because no interference layer follows them, so the readout bit reduces to a
parity expression. Tracking the basis bits `(a, b, c)`:

```
after CX(0,1): b <- b^a
after CX(1,2): c <- c^b^a
after CX(2,0): a <- a^(c^b^a) = b^c
```

The measured qubit equals `b XOR c`, so the Distance qubit cancels out
entirely. Numerical check: sweeping distance over [0,1] at fixed
(objects=0.3, vram=0.8) yields P_stress = 0.7378 at every point, matching the
analytic XOR prediction exactly.

## Finding 2: the Rz bias is a measurement no-op

Eq. 6 in the paper injects `Rz(0.60)` on the VRAM qubit as a risk aversion
bias, directly before the Z basis measurement. Phase rotations commute with Z
measurement: they change no measurement statistics unless a later gate mixes
the basis. Numerical check: max |P_with_bias - P_without_bias| is about 3e-16,
which is floating point zero, across the input cube.

## Finding 3: full range encoding wraps past |1>

`Ry(pi*x)` maps x=1 exactly onto |1>. Any additive rotation after that (an
entangler or the bias) rotates past the pole, which makes the stress response
non monotone: probability rises and then falls as VRAM grows, the opposite of
risk aversion at the critical end.

## The v2 circuit (implemented, verified)

| Change | v1 (paper) | v2 | Why |
|---|---|---|---|
| Encoding | Ry(pi*x) | Ry(pi/2*x) | rotation headroom, keeps the surface monotone |
| Entanglement | CNOT ring | CRy(pi/3) ring | soft monotone coupling instead of parity |
| Bias | Rz(0.60) | Ry(0.50) | population shift, the bias actually measures |
| Readout | q0 | q2 | all three inputs influence the score |
| Input 0 | distance | proximity (1-distance) | monotone increasing semantics |

Verified surface properties (exact simulation):

- Monotone in all three inputs, corners sane (all calm 0.06, all critical 0.91).
- VRAM dominant: 0.15 to 0.77 sweep at mid proximity and objects.
- Real entanglement with the paper's intended semantics: the proximity effect
  shrinks from +0.112 (vram=0.2) to +0.081 (vram=0.9). In other words, distance
  matters less when VRAM is critical.
- Bias effect up to 0.21 in stress probability.

## Status

`otuken_v1.yaml` stays in the repo for faithful reproduction of the published
paper. `otuken_v2.yaml` is the recommended configuration. A revised paper
should adopt the v2 circuit and cite these errata. The ablation protocol in
`qos-bench/` applies to v2.
