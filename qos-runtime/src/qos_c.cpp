// C ABI wrapper around the QOS C++ runtime.
// Copyright (c) 2026 Kaan Esrefoglu / Kestro Games. AGPL-3.0 (dual-licensed).

#define QOS_BUILDING
#include "qos/qos_c.h"

#include <exception>

#include "qos/qos.h"

struct qos_brain {
  qos::Brain brain;
};

extern "C" {

qos_brain* qos_brain_create(const char* model_path, int intra_op_threads) {
  if (model_path == nullptr) return nullptr;
  try {
    qos::Options options;
    options.intra_op_threads = intra_op_threads > 0 ? intra_op_threads : 1;
    return new qos_brain{qos::Brain(model_path, options)};
  } catch (const std::exception&) {
    return nullptr;
  }
}

int qos_brain_input_size(const qos_brain* brain) {
  if (brain == nullptr) return -1;
  return static_cast<int>(brain->brain.InputSize());
}

float qos_brain_evaluate(const qos_brain* brain, const float* inputs, int count) {
  if (brain == nullptr) return QOS_ERR_INVALID_HANDLE;
  if (inputs == nullptr || count < 0 ||
      static_cast<std::size_t>(count) != brain->brain.InputSize()) {
    return QOS_ERR_BAD_INPUT;
  }
  try {
    return brain->brain.Evaluate(inputs, static_cast<std::size_t>(count));
  } catch (const std::exception&) {
    return QOS_ERR_RUNTIME;
  }
}

void qos_brain_destroy(qos_brain* brain) { delete brain; }

const char* qos_version(void) { return "0.1.0"; }

}  // extern "C"
