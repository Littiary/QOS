# qos-runtime

C++ inference runtime for QOS models with a plain C ABI, so any engine or
language can embed it (C, C++, C#, Rust, Lua FFI, Python ctypes, custom engines
of every kind). The only dependency is ONNX Runtime.

## Build

1. Download a prebuilt ONNX Runtime package for your platform from
   [microsoft/onnxruntime releases](https://github.com/microsoft/onnxruntime/releases)
   (`onnxruntime-win-x64-*.zip`, `onnxruntime-linux-x64-*.tgz`, etc.) and extract it.
2. Configure and build:

```bash
cmake -B build -DONNXRUNTIME_DIR=/path/to/onnxruntime-win-x64-1.19.2
cmake --build build --config Release
```

3. Run the example against a model produced by `qos-core`:

```bash
./build/Release/qos_minimal ../qos-core/artifacts/otuken_v2/qos_model.onnx
```

## API

C++ (`qos/qos.h`):

```cpp
qos::Brain brain("qos_model.onnx");
float inputs[3] = {proximity, objects, vram};   // normalized [0,1]
float stress = brain.Evaluate(inputs, 3);       // [0,1]
```

C ABI (`qos/qos_c.h`), for FFI and custom engines:

```c
qos_brain* b = qos_brain_create("qos_model.onnx", 1);
float stress = qos_brain_evaluate(b, inputs, 3);
qos_brain_destroy(b);
```

Python (ctypes), handy for tooling and tests:

```python
import ctypes
lib = ctypes.CDLL("./qos.dll")  # or libqos.so
lib.qos_brain_create.restype = ctypes.c_void_p
lib.qos_brain_evaluate.restype = ctypes.c_float
brain = lib.qos_brain_create(b"qos_model.onnx", 1)
buf = (ctypes.c_float * 3)(0.5, 0.5, 0.9)
print(lib.qos_brain_evaluate(ctypes.c_void_p(brain), buf, 3))
```

## Design notes

- `Evaluate()` is thread safe, construction is not. One `Brain` per model is enough.
- `intra_op_threads = 1` on purpose: the model is tiny (3-64-64-1) and thread
  fan out costs more than it saves inside a frame budget.
- Negative return values from `qos_brain_evaluate` are error codes
  (`QOS_ERR_*` in `qos_c.h`). A valid stress score is always in `[0,1]`.
