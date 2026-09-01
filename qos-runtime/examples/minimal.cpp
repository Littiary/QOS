// Minimal QOS runtime example: load a model, sweep VRAM pressure, print
// the stress curve. Demonstrates the full integration surface an engine
// needs - about ten lines of real code.

#include <cstdio>

#include "qos/qos_c.h"

int main(int argc, char** argv) {
  const char* model = argc > 1 ? argv[1] : "qos_model.onnx";

  qos_brain* brain = qos_brain_create(model, /*intra_op_threads=*/1);
  if (brain == nullptr) {
    std::fprintf(stderr, "failed to load %s\n", model);
    return 1;
  }
  std::printf("QOS %s | model: %s | inputs: %d\n", qos_version(), model,
              qos_brain_input_size(brain));

  // Sweep VRAM pressure at fixed mid-range distance/object-count and watch
  // the entangled decision surface respond.
  std::printf("\n  vram   stress\n");
  for (int i = 0; i <= 10; ++i) {
    const float vram = static_cast<float>(i) / 10.0f;
    const float inputs[3] = {0.5f, 0.5f, vram};
    const float stress = qos_brain_evaluate(brain, inputs, 3);
    std::printf("  %.2f   %.4f\n", vram, stress);
  }

  qos_brain_destroy(brain);
  return 0;
}
