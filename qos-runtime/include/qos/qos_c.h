/* QOS - Quantum Optimization System
 * Pure C ABI for embedding QOS in any engine or language (C, C#, Rust,
 * Python ctypes, Lua FFI, ...). Wraps the C++ runtime in qos.h.
 *
 * Copyright (c) 2026 Kaan Esrefoglu / Kestro Games. AGPL-3.0 (dual-licensed).
 */

#ifndef QOS_C_H
#define QOS_C_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(QOS_SHARED)
#  ifdef QOS_BUILDING
#    define QOS_API __declspec(dllexport)
#  else
#    define QOS_API __declspec(dllimport)
#  endif
#else
#  define QOS_API
#endif

typedef struct qos_brain qos_brain;

/* Error codes returned by qos_brain_evaluate. */
#define QOS_ERR_INVALID_HANDLE (-1.0f)
#define QOS_ERR_BAD_INPUT      (-2.0f)
#define QOS_ERR_RUNTIME        (-3.0f)

/* Loads a QOS ONNX model. Returns NULL on failure.
 * intra_op_threads: threads per evaluation; use 1 for game loops. */
QOS_API qos_brain* qos_brain_create(const char* model_path, int intra_op_threads);

/* Number of input features the model expects, or -1 on invalid handle. */
QOS_API int qos_brain_input_size(const qos_brain* brain);

/* Evaluates one scenario of `count` normalized [0,1] inputs.
 * Returns the stress score in [0,1], or a negative QOS_ERR_* code. */
QOS_API float qos_brain_evaluate(const qos_brain* brain, const float* inputs, int count);

/* Frees the brain. Safe to call with NULL. */
QOS_API void qos_brain_destroy(qos_brain* brain);

/* Library version string, e.g. "0.1.0". */
QOS_API const char* qos_version(void);

#ifdef __cplusplus
}
#endif

#endif /* QOS_C_H */
