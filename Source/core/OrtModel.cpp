#include "OrtModel.h"

#if SL_HAVE_ORT && defined (_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include <onnxruntime_c_api.h>

#include <mutex>

namespace sl::ort
{

namespace
{
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;   // one per process, deliberately never released
    std::mutex loadLock;

    /** Turns a failed call into a message and frees the status. */
    bool failed (OrtStatus* status, std::string& error)
    {
        if (status == nullptr)
            return false;

        error = api->GetErrorMessage (status);
        api->ReleaseStatus (status);
        return true;
    }

    class OrtModel final : public InferenceModel
    {
    public:
        ~OrtModel() override
        {
            if (memoryInfo != nullptr) api->ReleaseMemoryInfo (memoryInfo);
            if (session != nullptr)    api->ReleaseSession (session);
        }

        bool init (const void* data, size_t size, std::vector<int64_t> shape, std::string& error)
        {
            inputShape = std::move (shape);
            inputCount = 1;

            for (auto d : inputShape)
                inputCount *= (size_t) d;

            OrtSessionOptions* options = nullptr;

            if (failed (api->CreateSessionOptions (&options), error))
                return false;

            // Two threads is plenty for these small models; a host's audio and
            // UI threads need the rest. No spinning: idle workers must not
            // burn a core between frames.
            bool ok = ! failed (api->SetIntraOpNumThreads (options, 2), error)
                   && ! failed (api->SetInterOpNumThreads (options, 1), error)
                   && ! failed (api->SetSessionGraphOptimizationLevel (options, ORT_ENABLE_ALL), error)
                   && ! failed (api->AddSessionConfigEntry (options, "session.intra_op.allow_spinning", "0"), error)
                   && ! failed (api->AddSessionConfigEntry (options, "session.inter_op.allow_spinning", "0"), error)
                   && ! failed (api->CreateSessionFromArray (env, data, size, options, &session), error);

            api->ReleaseSessionOptions (options);

            if (! ok)
                return false;

            if (failed (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &memoryInfo), error))
                return false;

            OrtAllocator* allocator = nullptr;

            if (failed (api->GetAllocatorWithDefaultOptions (&allocator), error))
                return false;

            const auto readName = [&] (bool input, size_t index, std::string& out)
            {
                char* name = nullptr;
                auto* status = input ? api->SessionGetInputName (session, index, allocator, &name)
                                     : api->SessionGetOutputName (session, index, allocator, &name);

                if (failed (status, error))
                    return false;

                out = name;
                api->AllocatorFree (allocator, name);
                return true;
            };

            size_t numOutputs = 0;

            if (! readName (true, 0, inputName)
                || failed (api->SessionGetOutputCount (session, &numOutputs), error))
                return false;

            outputNames.resize (numOutputs);

            for (size_t i = 0; i < numOutputs; ++i)
                if (! readName (false, i, outputNames[i]))
                    return false;

            for (auto& n : outputNames)
                outputNamePtrs.push_back (n.c_str());

            return true;
        }

        bool run (const float* input, std::vector<std::vector<float>>& outputs) override
        {
            std::string error;
            OrtValue* in = nullptr;

            if (failed (api->CreateTensorWithDataAsOrtValue (memoryInfo, const_cast<float*> (input),
                                                             inputCount * sizeof (float),
                                                             inputShape.data(), inputShape.size(),
                                                             ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), error))
                return false;

            std::vector<OrtValue*> out (outputNames.size(), nullptr);
            const char* inName = inputName.c_str();

            const bool ok = ! failed (api->Run (session, nullptr, &inName, &in, 1,
                                                outputNamePtrs.data(), outputNamePtrs.size(), out.data()), error);
            api->ReleaseValue (in);

            outputs.resize (out.size());
            bool good = ok;

            for (size_t i = 0; i < out.size(); ++i)
            {
                if (out[i] == nullptr)
                {
                    good = false;
                    continue;
                }

                OrtTensorTypeAndShapeInfo* info = nullptr;
                size_t count = 0;
                float* data = nullptr;

                if (failed (api->GetTensorTypeAndShape (out[i], &info), error)
                    || failed (api->GetTensorShapeElementCount (info, &count), error)
                    || failed (api->GetTensorMutableData (out[i], (void**) &data), error))
                    good = false;
                else
                    outputs[i].assign (data, data + count);

                if (info != nullptr)
                    api->ReleaseTensorTypeAndShapeInfo (info);

                api->ReleaseValue (out[i]);
            }

            return good;
        }

    private:
        OrtSession* session = nullptr;
        OrtMemoryInfo* memoryInfo = nullptr;
        std::vector<int64_t> inputShape;
        size_t inputCount = 0;
        std::string inputName;
        std::vector<std::string> outputNames;
        std::vector<const char*> outputNamePtrs;
    };
}

bool loadRuntime (const std::wstring& dllPath, std::string& error)
{
    const std::lock_guard<std::mutex> lock (loadLock);

    if (api != nullptr && env != nullptr)
        return true;

    // A full path, so Windows' own (older) onnxruntime.dll in System32 is never
    // picked up instead; the altered search path lets the runtime find any
    // helper DLLs sitting next to it.
    wchar_t full[MAX_PATH * 4] {};
    const auto len = GetFullPathNameW (dllPath.c_str(), (DWORD) (sizeof (full) / sizeof (full[0])), full, nullptr);
    HMODULE module = LoadLibraryExW (len > 0 ? full : dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

    if (module == nullptr)
    {
        const auto code = GetLastError();
        error = code == ERROR_MOD_NOT_FOUND
              ? "onnxruntime.dll (or the Visual C++ runtime it needs) was not found"
              : "onnxruntime.dll could not be loaded (error " + std::to_string (code) + ")";
        return false;
    }

    using GetApiBase = const OrtApiBase* (ORT_API_CALL*) ();
    auto getBase = reinterpret_cast<GetApiBase> (reinterpret_cast<void*> (GetProcAddress (module, "OrtGetApiBase")));

    if (getBase == nullptr || getBase() == nullptr)
    {
        error = "onnxruntime.dll has no OrtGetApiBase";
        return false;
    }

    api = getBase()->GetApi (ORT_API_VERSION);

    if (api == nullptr)
    {
        error = std::string ("onnxruntime.dll is too old (") + getBase()->GetVersionString() + ")";
        return false;
    }

    if (failed (api->CreateEnv (ORT_LOGGING_LEVEL_ERROR, "sightline", &env), error))
    {
        env = nullptr;
        return false;
    }

    // Nothing leaves this machine.
    api->DisableTelemetryEvents (env);
    return true;
}

bool isRuntimeLoaded() noexcept
{
    return api != nullptr && env != nullptr;
}

std::unique_ptr<InferenceModel> createModel (const void* data, size_t size, std::vector<int64_t> shape,
                                             std::string& error)
{
    if (! isRuntimeLoaded())
    {
        error = "ONNX Runtime is not loaded";
        return nullptr;
    }

    auto model = std::make_unique<OrtModel>();

    if (! model->init (data, size, std::move (shape), error))
        return nullptr;

    return model;
}

} // namespace sl::ort

#else

namespace sl::ort
{

bool loadRuntime (const std::wstring&, std::string& error)
{
    error = "this build has no ONNX Runtime";
    return false;
}

bool isRuntimeLoaded() noexcept { return false; }

std::unique_ptr<InferenceModel> createModel (const void*, size_t, std::vector<int64_t>, std::string& error)
{
    error = "this build has no ONNX Runtime";
    return nullptr;
}

} // namespace sl::ort

#endif
