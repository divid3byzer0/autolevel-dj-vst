#include "Plugin.h"

#if defined(_WIN32) && !defined(ALDJ_NO_EDITOR)
#include "Editor.h"
#define ALDJ_HAS_EDITOR 1
#else
namespace aldj { class Editor {}; }
#define ALDJ_HAS_EDITOR 0
#endif

#include <cstring>

namespace aldj {

namespace {

constexpr int32_t fourCC(char a, char b, char c, char d) {
    return (static_cast<int32_t>(a) << 24) | (static_cast<int32_t>(b) << 16) | (static_cast<int32_t>(c) << 8) | static_cast<int32_t>(d);
}

constexpr int32_t kVersion = 0x30000; // 3.0.0, as the JUCE build reports it

void copyString(void* dst, const std::string& s, size_t maxLen) {
    if (dst == nullptr) return;
    auto* d = static_cast<char*>(dst);
    const size_t n = std::min(s.size(), maxLen);
    std::memcpy(d, s.data(), n);
    d[n] = '\0';
}

Plugin* self(vst2::AEffect* e) { return static_cast<Plugin*>(e->object); }

} // namespace

Plugin::Plugin(vst2::HostCallback host) : m_host(host) {
    for (int i = 0; i < kNumParams; ++i) m_values[static_cast<size_t>(i)].store(def(i).def);

    m_effect.magic = vst2::kMagic;
    m_effect.dispatcher = &dispatcherProc;
    m_effect.process = &processAccumulateProc;
    m_effect.setParameter = &setParameterProc;
    m_effect.getParameter = &getParameterProc;
    m_effect.numPrograms = 1;
    m_effect.numParams = kNumParams;
    m_effect.numInputs = 2;
    m_effect.numOutputs = 2;
    m_effect.flags = vst2::effFlagsCanReplacing | vst2::effFlagsProgramChunks
                   | (ALDJ_HAS_EDITOR ? vst2::effFlagsHasEditor : 0);
    m_effect.ioRatio = 1.0f;
    m_effect.object = this;
    m_effect.uniqueID = fourCC('A', 'l', 'D', 'j'); // the JUCE build's plugin code
    m_effect.version = kVersion;
    m_effect.processReplacing = &processReplacingProc;

    prepare(false); // runs at 44.1 kHz until the host sets its rate
}

Plugin::~Plugin() = default;

intptr_t Plugin::hostCall(int32_t op, int32_t index, intptr_t value, void* ptr, float opt) {
    return m_host != nullptr ? m_host(&m_effect, op, index, value, ptr, opt) : 0;
}

void Plugin::setHostValue(int i, float v) {
    if (i < 0 || i >= kNumParams) return;
    m_values[static_cast<size_t>(i)].store(snap(def(i), v), std::memory_order_relaxed);
}

void Plugin::beginEdit(int i) { hostCall(vst2::audioMasterBeginEdit, i); }
void Plugin::endEdit(int i) { hostCall(vst2::audioMasterEndEdit, i); }

void Plugin::setFromEditor(int i, float v) {
    const float before = hostValue(i);
    setHostValue(i, v);
    if (hostValue(i) != before) hostCall(vst2::audioMasterAutomate, i, 0, nullptr, normalised(i));
}

void Plugin::changeFromEditor(int i, float v) {
    beginEdit(i);
    setFromEditor(i, v);
    endEdit(i);
}

void Plugin::prepare(bool notifyHost) {
    // Like the JUCE build's prepareToPlay, but only when the sample rate changes: a player
    // suspends and resumes the plugin around track changes, and the box never resets its
    // loudness history on its own, so the plugin keeps it across tracks too.
    if (m_sampleRate == m_preparedRate) return;
    m_engine.prepare(m_sampleRate);
    m_preparedRate = m_sampleRate;
    for (auto& ch : m_in) ch.fill(0.0f);
    for (auto& ch : m_out) ch.fill(0.0f);
    m_fifoPos = 0;
    const int delay = BOX_BLOCK + m_engine.latencyFrames();
    if (delay != m_effect.initialDelay) {
        m_effect.initialDelay = delay;
        if (notifyHost) hostCall(vst2::audioMasterIOChanged);
    }
}

void Plugin::pushParamsToEngine() {
    for (int i = 0; i < kNumParams; ++i)
        m_engine.setParam(static_cast<Param>(i), toEngineValue(i, hostValue(i)));
}

void Plugin::process(float** in, float** out, int32_t frames, bool accumulate) {
    if (frames <= 0 || in == nullptr || out == nullptr) return;
    const float* inL = in[0];
    const float* inR = in[1];
    float* outL = out[0];
    float* outR = out[1];

    for (int32_t s = 0; s < frames;) {
        // Swap as much as fits before the current box block is full (in may alias out).
        const int take = std::min(static_cast<int>(frames - s), BOX_BLOCK - m_fifoPos);
        for (int k = 0; k < take; ++k) {
            const size_t f = static_cast<size_t>(m_fifoPos + k);
            const float l = inL[s + k];
            const float r = inR[s + k];
            if (accumulate) {
                outL[s + k] += m_out[0][f];
                outR[s + k] += m_out[1][f];
            } else {
                outL[s + k] = m_out[0][f];
                outR[s + k] = m_out[1][f];
            }
            m_in[0][f] = l;
            m_in[1][f] = r;
        }
        m_fifoPos += take;
        s += take;

        if (m_fifoPos == BOX_BLOCK) {
            // What the box's JACK callback does once per period: pick up the current settings,
            // then run one 256-frame block.
            pushParamsToEngine();
            m_engine.process(m_in[0].data(), m_in[1].data(), m_out[0].data(), m_out[1].data(),
                             static_cast<size_t>(BOX_BLOCK));
            m_fifoPos = 0;
        }
    }
}

std::string Plugin::presetJson() const {
    Engine snapshot;
    for (int i = 0; i < kNumParams; ++i)
        snapshot.setParam(static_cast<Param>(i), toEngineValue(i, hostValue(i)));
    return snapshot.getStateJson();
}

void Plugin::loadPresetJson(const std::string& json, bool includeBypass, bool notifyHost) {
    Engine parsed;                  // box defaults...
    parsed.loadStateFromJson(json); // ...overwritten by whatever the preset has, via the box's loader
    for (int i = 0; i < kNumParams; ++i) {
        const auto p = static_cast<Param>(i);
        if (p == Param::BYPASS && !includeBypass) continue; // a preset never bypasses the plugin
        const float v = toHostValue(i, parsed.getParam(p));
        if (notifyHost) changeFromEditor(i, v);
        else setHostValue(i, v);
    }
}

void Plugin::resetToDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        if (static_cast<Param>(i) != Param::BYPASS) changeFromEditor(i, def(i).def);
}

intptr_t Plugin::dispatch(int32_t op, int32_t index, intptr_t value, void* ptr, float opt) {
    using namespace vst2;
    switch (op) {
        case effOpen: return 0;
        case effClose:
#if ALDJ_HAS_EDITOR
            if (m_editor) m_editor->close();
#endif
            delete this;
            return 1;

        case effSetProgram: return 0;
        case effGetProgram: return 0;
        case effSetProgramName: return 0;
        case effGetProgramName: copyString(ptr, "Default", 24); return 0;
        case effGetProgramNameIndexed:
            if (index != 0) return 0;
            copyString(ptr, "Default", 24);
            return 1;

        case effGetParamLabel:
            if (index < 0 || index >= kNumParams) return 0;
            copyString(ptr, def(index).label, 8);
            return 0;
        case effGetParamDisplay:
            if (index < 0 || index >= kNumParams) return 0;
            copyString(ptr, valueText(index, hostValue(index)), 24);
            return 0;
        case effGetParamName:
            if (index < 0 || index >= kNumParams) return 0;
            copyString(ptr, def(index).name, 32);
            return 0;
        case effCanBeAutomated: return (index >= 0 && index < kNumParams) ? 1 : 0;
        case effString2Parameter: {
            if (index < 0 || index >= kNumParams || ptr == nullptr) return 0;
            float v;
            if (!parseText(index, static_cast<const char*>(ptr), v)) return 0;
            setHostValue(index, v);
            return 1;
        }

        case effSetSampleRate:
            if (opt > 0.0f) m_sampleRate = static_cast<double>(opt);
            return 0;
        case effSetBlockSize: return 0;
        case effMainsChanged:
            if (value != 0) prepare(true);
            return 0;
        case effStartProcess:
        case effStopProcess: return 0;

        case effGetChunk: {
            if (ptr == nullptr) return 0;
            m_chunk = presetJson();
            *static_cast<void**>(ptr) = m_chunk.data();
            return static_cast<intptr_t>(m_chunk.size());
        }
        case effSetChunk: {
            if (ptr == nullptr || value <= 0) return 0;
            const std::string json(static_cast<const char*>(ptr), static_cast<size_t>(value));
            if (json.find('{') == std::string::npos) return 0; // not ours
            loadPresetJson(json, true, false);
            hostCall(audioMasterUpdateDisplay);
            return 1;
        }

        case effSetBypass:
            setHostValue(static_cast<int>(Param::BYPASS), value != 0 ? 1.0f : 0.0f);
            return 1;

        case effGetPlugCategory: return kPlugCategEffect;
        case effGetEffectName: copyString(ptr, "AutoLevel DJ", 32); return 1;
        case effGetVendorString: copyString(ptr, "DistrictNoir", 64); return 1;
        case effGetProductString: copyString(ptr, "AutoLevel DJ", 64); return 1;
        case effGetVendorVersion: return kVersion;
        case effGetVstVersion: return 2400;
        case effGetTailSize: return 0;
        case effCanDo: {
            if (ptr == nullptr) return 0;
            const std::string what(static_cast<const char*>(ptr));
            if (what == "bypass" || what == "plugAsChannelInsert" || what == "plugAsSend" || what == "2in2out")
                return 1;
            if (what == "receiveVstEvents" || what == "receiveVstMidiEvent" || what == "sendVstEvents"
                || what == "sendVstMidiEvent")
                return -1;
            return 0;
        }
        case effProcessEvents: return 0;

#if ALDJ_HAS_EDITOR
        case effEditGetRect:
            if (ptr == nullptr) return 0;
            if (!m_editor) m_editor = std::make_unique<Editor>(*this);
            *static_cast<ERect**>(ptr) = m_editor->rect();
            return 1;
        case effEditOpen:
            if (!m_editor) m_editor = std::make_unique<Editor>(*this);
            return m_editor->open(ptr) ? 1 : 0;
        case effEditClose:
            if (m_editor) m_editor->close();
            return 1;
        case effEditIdle:
            if (m_editor) m_editor->idle();
            return 0;
#endif
        default: return 0;
    }
}

intptr_t VST2_CALL Plugin::dispatcherProc(vst2::AEffect* e, int32_t op, int32_t index, intptr_t value, void* ptr, float opt) {
    return self(e)->dispatch(op, index, value, ptr, opt);
}
void VST2_CALL Plugin::processReplacingProc(vst2::AEffect* e, float** in, float** out, int32_t frames) {
    self(e)->process(in, out, frames, false);
}
void VST2_CALL Plugin::processAccumulateProc(vst2::AEffect* e, float** in, float** out, int32_t frames) {
    self(e)->process(in, out, frames, true);
}
void VST2_CALL Plugin::setParameterProc(vst2::AEffect* e, int32_t index, float value) {
    self(e)->setNormalised(index, value);
}
float VST2_CALL Plugin::getParameterProc(vst2::AEffect* e, int32_t index) {
    if (index < 0 || index >= kNumParams) return 0.0f;
    return self(e)->normalised(index);
}

} // namespace aldj

extern "C" {

#if defined(_WIN32)
#define ALDJ_EXPORT __declspec(dllexport)
#else
#define ALDJ_EXPORT __attribute__((visibility("default")))
#endif

ALDJ_EXPORT vst2::AEffect* VST2_CALL VSTPluginMain(vst2::HostCallback host) {
    if (host == nullptr || host(nullptr, vst2::audioMasterVersion, 0, 0, nullptr, 0.0f) == 0) return nullptr;
    auto* plugin = new aldj::Plugin(host);
    return plugin->effect();
}

} // extern "C"
