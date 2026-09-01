# qos-bench

The benchmark protocol that turns QOS from an architecture into a measured
system, including the quantum vs classical ablation.

## The four arms

Every benchmark run compares four configurations in the same scene, same camera
path, same hardware, same build:

| Arm | Name | What it is |
|---|---|---|
| A | `baseline` | No adaptive optimization at all |
| B | `threshold` | Classic static heuristic (whitepaper Eq. 1 style) |
| C | `classical-nn` | The same student network trained on hand tuned logistic labels (`configs/ablation_logistic.yaml`) |
| D | `qos` | Student trained on VQC labels (`configs/otuken_v2.yaml`) |

Arm C is the one that isolates the quantum contribution. It shares the network,
training procedure and integration path with D, only the label source differs.
If D beats C, the VQC prior earns its place. If it does not, that gets published
as is.

## Metrics

Average FPS hides exactly the problems this system exists to fix, so the
analyzer reports the tail instead:

- 1% low FPS (frame rate of the worst 1% of frames)
- Frame time p95 and p99 in ms
- Frame time standard deviation (oscillation indicator)
- Stutter count: frames above 33.3 ms, plus frames above 50 ms as severe
- Mean FPS last, for context only

## Producing input data

Any engine can participate: dump one CSV per arm with a `frame_time_ms` column,
one row per frame. Optional `vram_mb` and `stress` columns are included in the
report when present.

- Unreal: a camera path replay over Epic's free City Sample is the reference
  scene, since anyone can download it and reproduce the numbers. `stat
  unitgraph` / csv profiler output converts easily.
- Custom engines: log `frame_time_ms` from your main loop during a scripted
  replay.

Keep runs at 60 seconds or longer and repeat each arm at least 3 times, then
report the median run.

## Analyzing

```bash
python analyze.py runs/baseline.csv runs/threshold.csv runs/classical-nn.csv runs/qos.csv
# or explicitly labeled:
python analyze.py --labels baseline,threshold,classical-nn,qos runs/*.csv
```

Prints a comparison table and writes `report.md` next to the inputs.

## Ground rules

1. Publish the exact configs, scene, camera path and hardware specs with every
   result.
2. No cherry picking. Medians of 3 or more runs, outliers reported.
3. Negative and null results get published with the same prominence as wins.
