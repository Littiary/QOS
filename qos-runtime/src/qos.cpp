// QOS C++ runtime implementation on top of ONNX Runtime.
// Copyright (c) 2026 Kaan Esrefoglu / Kestro Games. AGPL-3.0 (dual-licensed).

#include "qos/qos.h"

#include <onnxruntime_cxx_api.h>

#include <array>
#include <stdexcept>
#include <vector>

namespace qos {

namespace {

constexpr const char* kInputName = "game_state";
constexpr const char* kOutputName = "stress_score";

#ifdef _WIN32
std::wstring ToWide(const std::string& s) {
  return std::wstring(s.begin(), s.end());
}
#endif

}  // namespace

struct Brain::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "qos"};
  Ort::SessionOptions session_options;
  std::unique_ptr<Ort::Session> session;
  Ort::MemoryInfo memory_info{
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
  std::size_t input_size = 0;
};

Brain::Brain(const std::string& model_path, const Options& options)
    : impl_(std::make_unique<Impl>()) {
  impl_->session_options.SetIntraOpNumThreads(options.intra_op_threads);
  impl_->session_options.SetGraphOptimizationLevel(
      GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
  impl_->session = std::make_unique<Ort::Session>(
      impl_->env, ToWide(model_path).c_str(), impl_->session_options);
#else
  impl_->session = std::make_unique<Ort::Session>(
      impl_->env, model_path.c_str(), impl_->session_options);
#endif

  if (impl_->session->GetInputCount() != 1 ||
      impl_->session->GetOutputCount() != 1) {
    throw std::runtime_error("QOS model must have exactly one input and one output");
  }

  auto shape = impl_->session->GetInputTypeInfo(0)
                   .GetTensorTypeAndShapeInfo()
                   .GetShape();
  // Expected shape: [batch (dynamic, -1), n_features].
  if (shape.size() != 2 || shape[1] <= 0) {
    throw std::runtime_error("QOS model input must be [batch, n_features]");
  }
  impl_->input_size = static_cast<std::size_t>(shape[1]);
}

Brain::~Brain() = default;
Brain::Brain(Brain&&) noexcept = default;
Brain& Brain::operator=(Brain&&) noexcept = default;

std::size_t Brain::InputSize() const { return impl_->input_size; }

float Brain::Evaluate(const float* inputs, std::size_t count) const {
  if (inputs == nullptr || count != impl_->input_size) {
    throw std::invalid_argument("QOS Evaluate: input size mismatch");
  }

  const std::array<int64_t, 2> shape{1, static_cast<int64_t>(count)};
  Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
      impl_->memory_info, const_cast<float*>(inputs), count, shape.data(),
      shape.size());

  const char* input_names[] = {kInputName};
  const char* output_names[] = {kOutputName};
  auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, input_names,
                                     &input_tensor, 1, output_names, 1);
  return outputs[0].GetTensorData<float>()[0];
}

}  // namespace qos
