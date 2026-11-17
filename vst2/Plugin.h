#pragma once
#include "Params.h"
#include "vst2_abi.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace aldj {

class Editor;

/**
 * AutoLevel DJ as a VST2 plugin: the autolevel-box chain (autolevel-core's AudioEngine), run
 * exactly as the JUCE build runs it - one host parameter per engine parameter, and the engine
 * always fed BOX_BLOCK-frame blocks (the box's JACK period), at BOX_BLOCK frames of latency.
 */
class Plugin {
public:
    static constexpr int BOX_BLOCK = 256;

    explicit Plugin(vst2::HostCallback host);
    ~Plugin();

    vst2::AEffect* effect() { return &m_effect; }

    // --- parameters (any thread)
    float hostValue(int i) const { return m_values[static_cast<size_t>(i)].load(std::memory_order_relaxed); }
    float hostValue(Param p) const { return hostValue(static_cast<int>(p)); }
    float normalised(int i) const { return toNormalised(i, hostValue(i)); }
    void setNormalised(int i, float norm) { setHostValue(i, fromNormalised(i, norm)); }
    void setHostValue(int i, float v);

    // --- editor side: changes that also tell the host (automation)
    void beginEdit(int i);
    void endEdit(int i);
    /** Set from the editor, notifying the host. */
    void setFromEditor(int i, float hostValue);
    /** A one-shot change (button, menu): begin, set, end. */
    void changeFromEditor(int i, float hostValue);

    autolevel::dsp::EngineVisualState visualState() const { return m_engine.getVisualState(); }
    void resetIntegration() { m_engine.reset(); }

    /** Current settings as a box preset (the exact JSON autolevel-box saves). */
    std::string presetJson() const;
    /** Load a box preset through the box's own loader; missing fields take the box defaults. */
    void loadPresetJson(const std::string& json, bool includeBypass, bool notifyHost);
    void resetToDefaults();

    // --- host
    intptr_t hostCall(int32_t op, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0.0f);

    // Window size the editor last had (kept while the plugin lives).
    float editorScale = 1.0f;

private:
    static intptr_t VST2_CALL dispatcherProc(vst2::AEffect*, int32_t, int32_t, intptr_t, void*, float);
    static void VST2_CALL processReplacingProc(vst2::AEffect*, float**, float**, int32_t);
    static void VST2_CALL processAccumulateProc(vst2::AEffect*, float**, float**, int32_t);
    static void VST2_CALL setParameterProc(vst2::AEffect*, int32_t, float);
    static float VST2_CALL getParameterProc(vst2::AEffect*, int32_t);

    intptr_t dispatch(int32_t op, int32_t index, intptr_t value, void* ptr, float opt);
    void process(float** in, float** out, int32_t frames, bool accumulate);
    void prepare(bool notifyHost);
    void pushParamsToEngine();

    vst2::AEffect m_effect{};
    vst2::HostCallback m_host;
    std::array<std::atomic<float>, kNumParams> m_values;

    Engine m_engine;
    double m_sampleRate = 44100.0;
    double m_preparedRate = 0.0;

    std::array<std::array<float, BOX_BLOCK>, 2> m_in{};
    std::array<std::array<float, BOX_BLOCK>, 2> m_out{};
    int m_fifoPos = 0;

    std::string m_chunk; // returned by effGetChunk, valid until the next call
    std::unique_ptr<Editor> m_editor;
};

} // namespace aldj
