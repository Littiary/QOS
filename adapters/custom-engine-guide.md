# Integrating QOS into a Custom Engine

QOS targets your engine, not any specific commercial one. The contract is small:
you feed normalized floats in, you get a stress score out. What to measure and
what to throttle stays under your engine's control.

## The integration contract

```
your engine                          QOS
-----------                          ---
collect N metrics ---normalize---->  qos_brain_evaluate(...)
apply decisions   <--stress [0,1]--  (well under 0.05 ms on CPU)
```

1. Link `qos-runtime` (C ABI in `qos/qos_c.h`, or the C++ API in `qos/qos.h`).
2. Load the model once at startup: `qos_brain_create("qos_model.onnx", 1)`.
3. Feed it your normalized metrics on each evaluation tick.
4. Map the score to actions with your own thresholds and curves.

## Step by step

### 1. Choose your input vector

The reference model uses `[proximity, object_count, vram_usage]`, but the
pipeline is fully parametric. Edit `qos-core/configs/*.yaml` (`oracle.inputs`,
`oracle.n_qubits`) and retrain to match whatever your engine actually measures:
CPU load, draw calls, audio voices, network latency. One qubit per input.

Normalization is your responsibility and has to match training: every input is
mapped to `[0,1]` (for example `vram_used / vram_budget`) and clamped at the
edges.

### 2. Pick an evaluation cadence

Do not evaluate every frame. Resource states do not change that fast, and
decisions that flip every 16 ms cause visible oscillation. Patterns that work:

- Evaluate every N frames (10 to 30) or on a fixed timer (100 to 250 ms).
- Or event driven: evaluate when any input moved more than some delta since the
  last evaluation.

### 3. Smooth and add hysteresis

Raw scores should never drive actions directly.

```c
/* Exponential moving average kills jitter */
smoothed = 0.8f * smoothed + 0.2f * stress;

/* Hysteresis prevents flip-flopping at a boundary */
if (!degraded && smoothed > 0.70f) { degraded = true;  apply_low_settings(); }
if ( degraded && smoothed < 0.55f) { degraded = false; apply_high_settings(); }
```

The gap between 0.70 and 0.55 is the hysteresis band. Tune it per subsystem.

### 4. Map score bands to actions (example)

| Smoothed score | Meaning | Typical action |
|---|---|---|
| 0.00-0.40 | Stable | Full quality, optionally restore previously degraded settings |
| 0.40-0.70 | Pressure building | Soft measures: streaming pool trim, distant tick rate reduction |
| 0.70-1.00 | Critical | Hard measures: LOD bias, shadow resolution, substep reduction |

### 5. Scale to a Council (multiple agents)

One `qos_brain` instance is one specialist. The whitepaper's Council pattern is
just N models with N input vectors:

| Agent | Watches | Controls |
|---|---|---|
| Governor | distance, density, velocity | LOD, culling, instance density |
| Vizier | lights, exposure, scene complexity | shadows, GI quality |
| Gatekeeper | VRAM, unique textures, movement | streaming pool, mip bias |
| General | NPC count, physics load, CPU | tick intervals, substepping |

Train each one with its own config in `qos-core` (different `inputs`, same
pipeline), load each `.onnx` into its own brain, and stagger their evaluation
ticks so they never spike the same frame.

## Language bindings

The C ABI makes bindings short:

- C# (Unity or custom): `[DllImport("qos")] static extern float qos_brain_evaluate(IntPtr b, float[] inputs, int count);`
- Rust: declare the four functions in an `extern "C"` block, or generate with `bindgen qos_c.h`.
- Python (tooling and tests): see the ctypes snippet in [`../qos-runtime/README.md`](../qos-runtime/README.md).

## Failure behavior

Keep QOS advisory. If `qos_brain_create` returns NULL or evaluate returns a
negative error code, fall back to your static heuristics and log once. The game
should behave identically minus the adaptive optimization.
