# Unreal Engine 5 Adapter (optional)

Unreal is one consumer of QOS, not its home. UE projects have two paths.

## Path A: UE's built-in NNE (no extra libraries)

UE 5.3+ ships the Neural Network Engine (NNE), which runs ONNX directly, so a
UE project does not even need `qos-runtime`:

1. Enable the NNERuntimeORT plugin.
2. Import `qos_model.onnx`. It becomes a `UNNEModelData` asset.
3. In a C++ actor (the whitepaper's `QuantumBrainActor` pattern):
   acquire the runtime, create the model, create an instance, call `RunSync()`.
4. Expose a `BlueprintCallable` like
   `GetQuantumStressLevel(Distance, ObjCount, VramUsage)` and drive cvars
   (`r.StaticMeshLODDistanceScale`, `r.Streaming.PoolSize`,
   `r.ShadowQuality`, `p.Chaos.Solver.NumSubsteps`, ...).

### UE 5.6 NNE API notes (from the whitepaper, section 6.2)

- `INNERuntimeCPU` and related interfaces moved to the global namespace. Drop
  stale `UE::NNE::` prefixes where the compiler complains.
- `TWeakInterfacePtr` can no longer be dereferenced with `->`. Call `.Get()`
  first and null check the raw pointer.
- Model creation is now a three step chain:
  `Runtime->CreateModelCPU(ModelData)` returns an `IModelCPU`, then
  `CreateModelInstanceCPU()` yields an `IModelInstanceCPU`.
- Input shapes are set via `SetInputTensorShapes()` on the model instance.
  `FTensorBindingCPU` no longer exposes a shape member.
- Prefer `NNERuntimeORTCpu`, fall back to `NNERuntimeRDG`.

## Path B: link qos-runtime as a ThirdParty module

Useful when you want identical behavior across UE and non-UE builds of the same
game: wrap `qos-runtime` as a UE ThirdParty module and call the C ABI. One
inference codepath everywhere.

## Status

A packaged UE plugin (Fab release) is on the roadmap and this directory will
host its source. The cadence and hysteresis guidance in
[`../custom-engine-guide.md`](../custom-engine-guide.md) applies to UE too:
drive cvars from smoothed, hysteresis guarded scores, never raw ones.
