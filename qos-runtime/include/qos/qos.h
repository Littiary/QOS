// QOS - Quantum Optimization System
// Engine-agnostic C++ inference runtime for QOS ONNX models.
//
// Copyright (c) 2026 Kaan Esrefoglu / Kestro Games. AGPL-3.0 (dual-licensed).

#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace qos {

struct Options {
  // Threads used inside a single Evaluate() call. Keep at 1 for game loops:
  // the model is tiny and thread fan-out costs more than it saves.
  int intra_op_threads = 1;
};

// Loads a QOS ONNX model and evaluates stress scores.
//
// Thread-safety: Evaluate() is safe to call concurrently from multiple
// threads on the same Brain instance (ONNX Runtime sessions are
// thread-safe for Run). Construction/destruction are not.
class Brain {
 public:
  // Throws std::runtime_error if the model cannot be loaded or has an
  // unexpected signature (expects input "game_state" [batch, n] and
  // output "stress_score" [batch, 1]).
  explicit Brain(const std::string& model_path, const Options& options = {});
  ~Brain();

  Brain(Brain&&) noexcept;
  Brain& operator=(Brain&&) noexcept;
  Brain(const Brain&) = delete;
  Brain& operator=(const Brain&) = delete;

  // Number of input features the model expects (3 for the reference
  // Otüken model: distance, object_count, vram_usage).
  std::size_t InputSize() const;

  // Evaluates one scenario. `inputs` must hold `count == InputSize()`
  // values normalized to [0, 1]. Returns the stress score in [0, 1].
  // Throws std::invalid_argument on size mismatch.
  float Evaluate(const float* inputs, std::size_t count) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qos
