# Unreal Engine 5.8 benchmark results

First measured comparison of the four arms on the reference machine. Numbers
in this file come straight from `analyze.py` over the CSVs the harness writes;
nothing is hand edited. The protocol, tiers and stress settings are described
in [README.md](README.md).

## Setup

| Item | Value |
|---|---|
| Engine | Unreal Engine 5.8, DX12, Lumen GI, virtual shadow maps, Nanite |
| Scene | `/Game/deneme` from the Otuken project: yurt camp on a Brushify landscape with Ultra Dynamic Sky |
| Resolution | 3840 x 2160, windowed, vsync off, no frame cap |
| Base quality | all scalability groups pinned to Cinematic (4) for every arm |
| Stress | 128 shadowed movable point lights and 96 copies of the three heaviest placed meshes, switched on and off in two waves per run |
| Texture pool | 1500 MB (`r.Streaming.PoolSize`) |
| Camera | deterministic terrain following figure-8 around the placed meshes, eye height 180 cm |
| Runs | 4 arms x 3 runs x 60 s measured, 10 s warmup each, median run reported |
| Policy interval | 0.2 s, EMA alpha 0.2, tier bands 0.35 / 0.55 / 0.75 with 0.10 hysteresis |
| Tier knobs | dynamic resolution 100 / 90 / 80 / 65 percent and Nanite pixels per edge 1 / 1.5 / 2 / 3 |
| Machine | RTX 5090 32 GB (driver 610.62), Core Ultra 9 285K, 96 GB DDR5 6600, Windows 11 |
| Models | `qos`: otuken_v2 (3 inputs: proximity, object density, VRAM pressure); `classical`: same network trained on logistic labels |

## Arms

- baseline: tier 0 for the whole run
- threshold: static rule, tier 2 when `(proximity > 0.65 and density > 0.6) or vram > 0.8`, else tier 0
- classical: student network trained on hand tuned logistic labels
- qos: student network trained on VQC labels

classical and qos share every line of code except the model file.

## Experiment 1: 3 input models (whitepaper configuration)

128 lights, 96 meshes, 3 runs per arm. Models: `otuken_v2` (qos) and `ablation_logistic` (classical).

| arm | frames | mean_fps | low_1pct_fps | p95_ms | p99_ms | std_ms | over_budget | stutters | severe | mean_tier | tier_changes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline_run1 | 3182 | 53.0 | 23.5 | 35.09 | 38.60 | 9.66 | 1517 | 380 | 1 | 0.00 | 0 |
| baseline_run2 | 2507 | 41.8 | 20.2 | 41.73 | 45.40 | 11.12 | 1464 | 656 | 4 | 0.00 | 0 |
| baseline_run3 | 2837 | 47.3 | 23.4 | 36.70 | 38.88 | 9.92 | 1507 | 552 | 2 | 0.00 | 0 |
| threshold_run1 | 2870 | 47.8 | 22.3 | 35.79 | 40.33 | 9.64 | 1536 | 388 | 1 | 0.61 | 15 |
| threshold_run2 | 2590 | 43.2 | 20.6 | 39.86 | 44.16 | 10.91 | 1476 | 744 | 3 | 0.58 | 11 |
| threshold_run3 | 2986 | 49.8 | 23.3 | 35.14 | 39.26 | 9.75 | 1509 | 341 | 1 | 0.60 | 11 |
| classical_run1 | 3360 | 56.0 | 24.7 | 33.38 | 35.40 | 9.53 | 1529 | 183 | 3 | 2.15 | 10 |
| classical_run2 | 3478 | 58.0 | 27.2 | 31.77 | 33.18 | 8.92 | 1610 | 28 | 1 | 2.24 | 6 |
| classical_run3 | 3419 | 57.0 | 26.4 | 32.68 | 34.49 | 9.16 | 1617 | 110 | 1 | 2.18 | 10 |
| qos_run1 | 3016 | 50.3 | 23.6 | 35.64 | 38.13 | 9.70 | 1519 | 422 | 2 | 2.25 | 7 |
| qos_run2 | 3363 | 56.0 | 27.1 | 31.75 | 33.31 | 8.77 | 1584 | 36 | 1 | 2.23 | 7 |
| qos_run3 | 3248 | 54.1 | 25.3 | 33.34 | 35.60 | 9.06 | 1560 | 165 | 1 | 2.26 | 7 |
| baseline (median of 3) | 2837 | 47.3 | 23.4 | 36.70 | 38.88 | 9.92 | 1507 | 552 | 2 | 0.00 | 0 |
| threshold (median of 3) | 2870 | 47.8 | 22.3 | 35.79 | 40.33 | 9.75 | 1509 | 388 | 1 | 0.60 | 11 |
| classical (median of 3) | 3419 | 57.0 | 26.4 | 32.68 | 34.49 | 9.16 | 1610 | 110 | 1 | 2.18 | 10 |
| qos (median of 3) | 3248 | 54.1 | 25.3 | 33.34 | 35.60 | 9.06 | 1560 | 165 | 1 | 2.25 | 7 |

## Reading the table

- `over_budget` counts frames above 16.67 ms (missed 60 FPS). `stutters` are frames above 33.3 ms, `severe` above 50 ms.
- `mean_tier` is the quality the arm gave up: 0 is full quality, 3 is the lowest tier. An arm that wins frame times at a much higher mean tier is buying them with visible quality.
- `tier_changes` is how often the policy flipped. Every change still costs a slightly longer frame (see the knob costs in README.md), so fewer changes at equal load tracking is better.

### Observations

- On this scene and machine the two adaptive models land within run to run noise of each other. classical median: 57.0 mean FPS and 110 stutters; qos: 54.1 and 165, at practically the same mean tier (2.18 vs 2.25). qos changed tiers less often (7 vs 10 per run) and had the lowest frame time standard deviation, but those differences are smaller than the spread between runs of the same arm (classical stutters ranged from 28 to 183, qos from 36 to 422).
- Both adaptive arms clearly beat baseline and the static threshold rule: about 7 to 10 FPS more and a fifth to a third of the stutters, paid for by running most of the run at tier 2 to 3.
- The threshold rule flips 11 to 15 times per run and gains nothing measurable over baseline.
- over_budget is saturated for every arm: almost every frame is above 16.67 ms. The load is beyond what the two tier knobs can compensate (tier 3 still lands near 17 to 18 ms), so the 60 FPS metric does not separate the arms here. The next runs use a lighter stress setting so that the tiers can actually cross the budget line.
- Neither 3 input model sees the light wave, which is the dominant load driver; they react only through object density, proximity and VRAM pressure, which is why their tiers barely follow the wave. This is the strongest argument for the planned 8 input models, which get light count, frame load and camera speed as inputs.
- Run to run variance was large for every arm (baseline mean FPS 53.0 / 41.8 / 47.3 across three identical runs). Later runs will use five repeats and a check for background load.
- Verdict for the whitepaper configuration on this setup: parity with a hand tuned classical labeler. No quantum advantage is claimed from this run.

## Experiment 2: 8 input models (not completed)

Planned follow up with the same scene, machine, camera and tiers, but 64
lights and 48 meshes (so that the tiers can cross the 60 FPS line), 5 runs per
arm, and eight input models: `otuken_v3b_star` for qos (CRy ring between the
inputs plus a CRy star into a dedicated readout wire, see `paper/errata.md`
finding 4) and `ablation_logistic8` for classical (hand tuned logistic over
the same eight inputs). Both models see light count, frame load and camera
speed, so they can react to the stress wave directly.

Both models are trained and exported (`qos-core/artifacts/otuken_v3b_star`,
`qos-core/artifacts/ablation_logistic8`). The benchmark run itself was
interrupted on 2026-09-02 and has not been repeated yet, so there is no table
for this experiment. Expectation stated before running: parity, for the same
reason as experiment 1.

## Caveats

- One scene, one machine, one stress pattern. The stress wave is synthetic and the same for all arms; it is there to make the load move, not to imitate a specific game.
- In experiment 1 the qos and classical models are the 3 input models from the whitepaper configuration. They do not see the light count or the frame time, so they react to load only through object density, proximity and VRAM pressure. Experiment 2 addresses that with the eight input models.
- The scene assets are commercial packs and cannot be redistributed; a run on a public scene is the next step for outside reproduction.
