#pragma once
/*
 * The VST 2.4 binary interface: the struct layout, calling convention and opcode numbers a
 * VST2 host and plugin agree on. Written from the publicly documented ABI, so it needs no
 * Steinberg SDK. Only what this plugin uses is declared.
 */
#include <cstdint>

#if defined(_WIN32)
#define VST2_CALL __cdecl
#else
#define VST2_CALL
#endif

namespace vst2 {

struct AEffect;

using HostCallback = intptr_t(VST2_CALL*)(AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
using DispatcherProc = intptr_t(VST2_CALL*)(AEffect*, int32_t opcode, int32_t index, intptr_t value, void* ptr, float opt);
using ProcessProc = void(VST2_CALL*)(AEffect*, float** inputs, float** outputs, int32_t frames);
using ProcessDoubleProc = void(VST2_CALL*)(AEffect*, double** inputs, double** outputs, int32_t frames);
using SetParameterProc = void(VST2_CALL*)(AEffect*, int32_t index, float value);
using GetParameterProc = float(VST2_CALL*)(AEffect*, int32_t index);

constexpr int32_t kMagic = ('V' << 24) | ('s' << 16) | ('t' << 8) | 'P';

struct AEffect {
    int32_t magic;
    DispatcherProc dispatcher;
    ProcessProc process; // deprecated accumulating process
    SetParameterProc setParameter;
    GetParameterProc getParameter;
    int32_t numPrograms;
    int32_t numParams;
    int32_t numInputs;
    int32_t numOutputs;
    int32_t flags;
    intptr_t resvd1;
    intptr_t resvd2;
    int32_t initialDelay;
    int32_t realQualities;
    int32_t offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueID;
    int32_t version;
    ProcessProc processReplacing;
    ProcessDoubleProc processDoubleReplacing;
    char future[56];
};

struct ERect {
    int16_t top, left, bottom, right;
};

enum Flags : int32_t {
    effFlagsHasEditor = 1 << 0,
    effFlagsCanReplacing = 1 << 4,
    effFlagsProgramChunks = 1 << 5,
    effFlagsNoSoundInStop = 1 << 9,
};

enum EffectOpcodes : int32_t {
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effSetProgramName = 4,
    effGetProgramName = 5,
    effGetParamLabel = 6,
    effGetParamDisplay = 7,
    effGetParamName = 8,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13,
    effEditOpen = 14,
    effEditClose = 15,
    effEditIdle = 19,
    effGetChunk = 23,
    effSetChunk = 24,
    effProcessEvents = 25,
    effCanBeAutomated = 26,
    effString2Parameter = 27,
    effGetProgramNameIndexed = 29,
    effGetPlugCategory = 35,
    effSetBypass = 44,
    effGetEffectName = 45,
    effGetVendorString = 47,
    effGetProductString = 48,
    effGetVendorVersion = 49,
    effCanDo = 51,
    effGetTailSize = 52,
    effGetVstVersion = 58,
    effStartProcess = 71,
    effStopProcess = 72,
};

enum HostOpcodes : int32_t {
    audioMasterAutomate = 0,
    audioMasterVersion = 1,
    audioMasterIdle = 3,
    audioMasterIOChanged = 13,
    audioMasterSizeWindow = 15,
    audioMasterGetSampleRate = 16,
    audioMasterCanDo = 37,
    audioMasterUpdateDisplay = 42,
    audioMasterBeginEdit = 43,
    audioMasterEndEdit = 44,
};

constexpr int32_t kPlugCategEffect = 1;

} // namespace vst2
