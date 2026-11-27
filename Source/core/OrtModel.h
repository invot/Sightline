#pragma once

#include "HandLandmarker.h"

#include <memory>
#include <string>
#include <vector>

namespace sl::ort
{

/** Loads ONNX Runtime from an explicit path (onnxruntime.dll next to the app
    or plugin). Done at run time, not link time, so a missing or broken runtime
    leaves the app working with the fallback tracker rather than failing to
    load. Safe to call more than once; only the first successful call loads. */
bool loadRuntime (const std::wstring& dllPath, std::string& error);

bool isRuntimeLoaded() noexcept;

/** A model with one float input of the given shape, built from an in-memory
    .onnx file. Returns nullptr and sets error on failure. */
std::unique_ptr<InferenceModel> createModel (const void* onnxData, size_t onnxSize,
                                             std::vector<int64_t> inputShape,
                                             std::string& error);

} // namespace sl::ort
