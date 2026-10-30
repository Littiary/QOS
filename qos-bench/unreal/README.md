# Unreal Engine benchmark harness

A self contained benchmark subsystem for UE 5.8 that needs no changes to any
map. Drop the `Source/` files into a C++ project, build, and launch the game
with `-QOSArm=...`. The subsystem spawns its own camera, flies a deterministic
figure-8 path over the level, samples telemetry, drives quality settings
according to the selected arm's policy, logs every frame to CSV and quits.

## Files

| File | Role |
|---|---|
| `Source/QOSBrain.h/.cpp` | Loads a QOS `.onnx` straight from disk through UE's NNE (no asset import) and evaluates it. Input width is read from the model. |
| `Source/QOSTelemetry.h/.cpp` | Samples the eight canonical inputs: proximity, object density, VRAM pressure, movable light count, pawn count, draw calls, camera speed, frame load. |
| `Source/QOSBenchSubsystem.h/.cpp` | The runner: arms, camera path, policy, tiers, CSV. |
| `Source/Example.Build.cs` | Module rules showing the required dependencies (`NNE`, `RHI`, `RenderCore`). |
| `run_bench.ps1` | Launches all four arms N times in `-game` mode and runs `analyze.py` on the results. |

## Arms

| Arm | Policy |
|---|---|
| `baseline` | Tier 0 for the whole run, never changes anything |
| `threshold` | Static rule: `(proximity > 0.65 and density > 0.6) or vram > 0.8` jumps to tier 2, otherwise tier 0 |
| `classical` | Student network trained on logistic labels (`configs/ablation_logistic.yaml`), EMA smoothed score, tier bands with hysteresis |
| `qos` | Student network trained on VQC labels (`configs/otuken_v2.yaml`), same smoothing and bands |
| `cycle` | Diagnostic only: tier 0-1-2-3 every 3 s, used to measure knob switching cost |

The classical and qos arms share every line of code except the model file.
That is the ablation.

## Tiers

All arms use the same four quality tiers so only the decision policy differs:

Scalability groups are pinned to Cinematic (4) once at setup and never touched
again: switching `sg.*` groups mid-run recompiles shaders and turns every
decision into a 100+ ms hitch, which would measure the engine, not the policy.
Tiers only move continuous, hitch-free knobs, the same kind dynamic resolution
systems adjust every frame.

| Tier | Dynamic resolution (screen %) | Nanite px/edge |
|---|---|---|
| 0 | 100 | 1 |
| 1 | 90 | 1.5 |
| 2 | 80 | 2 |
| 3 | 65 | 3 |

Measured switching cost per knob on the reference machine (cycle arm, frame
right after a change, median / max ms): console screen percentage 38 / 53,
dynamic resolution 33 / 46, Nanite 27 / 53, foliage LOD scale 53 / 75 (and
several 100+ ms frames after), static mesh LOD scale 43 / 106. Applied
together the costs add up to the 150 to 230 ms hitches seen in the pilots, so
the default tier set keeps only dynamic resolution and Nanite.

The knobs a tier moves are selectable with `-QOSKnobs=` (`sp` = console
`r.ScreenPercentage`, `dynres` = `r.DynamicRes.TestScreenPercentage` through the
dynamic resolution system, `nanite`, `foliage`, `lod`; default `dynres,nanite`). The `cycle` arm walks
the tiers every 3 s regardless of telemetry, so the switching cost of each knob
can be measured on its own: run `-QOSArm=cycle -QOSKnobs=<one knob>` and look
at the frame after every `tier_changed` row. Virtual shadow map resolution bias
and view distance scale were dropped this way: each change invalidated every
shadow page (128 shadowed lights) or churned streaming for 150 ms.

Score bands (rising): 0.35 / 0.55 / 0.75, with 0.10 hysteresis on the way down.
EMA alpha 0.2 at a 0.2 s evaluation interval.

## Command line

```
UnrealEditor.exe "<project>.uproject" <map>?game=/Script/Engine.GameModeBase -game -windowed -ResX=3840 -ResY=2160
    -QOSArm=qos -QOSModel="<path>/qos_model.onnx" -QOSRun=1 -QOSDuration=60 -QOSWarmup=5
```

Optional: `-QOSEval=0.2` (policy interval), `-QOSRadius=` (camera orbit, auto
from the placed static meshes when omitted), `-QOSEye=180` (eye height above the
terrain), `-QOSHeight=` (fixed height instead of terrain following), `-QOSOut=`
(CSV folder, default `<Project>/Saved/QOSBench`).

## Stress injection

A 5090 does not break a sweat on most scenes, and a policy can only prove
itself under changing load. The harness therefore injects a reproducible load
wave on top of the scene:

- `-QOSStressLights=128`: shadowed movable point lights spread over the orbit
  area. They switch on and off following `0.5 * (1 - cos(2 * pi * 2 * t / T))`,
  so the load rises to its peak twice per run and falls back to zero.
- `-QOSStressMeshes=96`: copies of the three heaviest placed meshes, toggled by
  the same wave, so primitive counts and object density move with the load.
- `-QOSPool=1500`: overrides `r.Streaming.PoolSize` in MB to emulate a smaller
  GPU memory budget and make VRAM pressure a real input.

`run_bench.ps1` defaults to 3840x2160, 128 lights, 96 meshes and a 1500 MB pool.
Every arm gets exactly the same wave, so the only difference between runs is
how each policy reacts to it.

## CSV columns

`frame, time_s, frame_time_ms, game_ms, render_ms, draw_calls, primitives,
score, smoothed, tier, tier_changed, active_lights, active_meshes`, then the eight normalized inputs, then raw values
(`trace_cm, rendered_actors, pool_required_mb, pool_size_mb`) and the camera
position. `analyze.py` only needs `frame_time_ms`; the rest is there so the
decisions can be audited after the fact.

## Measurement hygiene

The subsystem sets `t.MaxFPS 0` and `r.VSync 0` at start and applies tier 0
before measuring, so every arm starts from the same state. During warmup the
full stress set is visible so shaders and PSOs compile before measurement
starts; warmup frames are not logged. Run each arm at least three times and report the median.
