/*
 * Test host for the VST2 build. Runs on Windows or under Wine:
 *     testhost.exe "AutoLevel DJ.dll" [screenshot-prefix]
 * Loads the plugin through VSTPluginMain like any VST2 host, then checks:
 *  1. audio: the plugin's output, fed in uneven host block sizes, equals the autolevel-core
 *     AudioEngine run directly in 256-frame blocks (the box's period), delayed 256 frames,
 *     bit for bit - with non-default settings set through the host parameters;
 *  2. state: effGetChunk -> effSetChunk into a second instance restores every parameter, and
 *     the chunk is a box-format preset;
 *  3. parameters: names, display text, defaults and normalised round trips;
 *  4. editor: opens at 840 x 700, and real mouse / keyboard messages change the parameters
 *     as the JUCE editor's controls do. Screenshots go to <prefix>_*.bmp.
 * Exit code 0 when everything passes.
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../vst2_abi.h"
#include "../Params.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace vst2;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<int> g_automated;
static int g_sizeW = 0, g_sizeH = 0;

static intptr_t VST2_CALL hostCb(AEffect*, int32_t op, int32_t index, intptr_t value, void* ptr, float) {
    switch (op) {
        case audioMasterVersion: return 2400;
        case audioMasterAutomate: g_automated.push_back(index); return 0;
        case audioMasterGetSampleRate: return 48000;
        case audioMasterCanDo: return (ptr && std::strcmp(static_cast<const char*>(ptr), "sizeWindow") == 0) ? 1 : 0;
        case audioMasterSizeWindow: g_sizeW = index; g_sizeH = static_cast<int>(value); return 1;
        default: return 0;
    }
}

using MainProc = AEffect*(VST2_CALL*)(HostCallback);

static intptr_t call(AEffect* e, int32_t op, int32_t index = 0, intptr_t value = 0, void* ptr = nullptr, float opt = 0.0f) {
    return e->dispatcher(e, op, index, value, ptr, opt);
}

/** A deterministic test signal: noise bursts and tones at changing levels, ~music dynamics. */
static void makeSignal(std::vector<float>& l, std::vector<float>& r, size_t n) {
    l.resize(n);
    r.resize(n);
    uint32_t seed = 12345;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<float>((seed >> 8) & 0xffff) / 32768.0f - 1.0f; };
    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        const float section = static_cast<float>((i / 48000) % 4);
        const float level = section == 0 ? 0.05f : section == 1 ? 0.5f : section == 2 ? 0.15f : 0.9f;
        const float kick = std::exp(-static_cast<float>(std::fmod(t, 0.5)) * 18.0f) * static_cast<float>(std::sin(2 * 3.14159265 * 55 * t));
        const float hat = rnd() * (std::fmod(t, 0.25) < 0.02 ? 0.6f : 0.05f);
        const float pad = 0.3f * static_cast<float>(std::sin(2 * 3.14159265 * 440 * t) + 0.5 * std::sin(2 * 3.14159265 * 3300 * t));
        l[i] = level * (kick + hat + pad);
        r[i] = level * (kick * 0.9f + hat * 1.1f + pad * 0.8f);
    }
}

static void saveBmp(HWND hwnd, const std::string& path) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    HDC wdc = GetDC(hwnd);
    HDC mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    // Ask the window to draw itself into our DC, so the capture does not depend on screen overlap.
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mdc), PRF_CLIENT);
    BitBlt(mdc, 0, 0, w, h, wdc, 0, 0, SRCCOPY);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        BITMAPFILEHEADER fh{};
        fh.bfType = 0x4d42;
        fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
        fh.bfSize = fh.bfOffBits + static_cast<DWORD>(w * h * 4);
        std::fwrite(&fh, sizeof fh, 1, f);
        std::fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        std::fwrite(bits, 4, static_cast<size_t>(w * h), f);
        std::fclose(f);
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(hwnd, wdc);
}

static void pump(int ms) {
    const DWORD end = GetTickCount() + static_cast<DWORD>(ms);
    do {
        MSG m;
        for (int k = 0; k < 50 && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++k) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(5);
    } while (static_cast<int>(GetTickCount() - end) < 0);
}

static LPARAM xy(int x, int y) { return MAKELPARAM(x, y); }

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { std::printf("usage: testhost <plugin.dll> [shot-prefix]\n"); return 2; }
    const std::string shots = argc > 2 ? argv[2] : "";
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { std::printf("cannot load %s\n", argv[1]); return 2; }
    auto entry = reinterpret_cast<MainProc>(GetProcAddress(dll, "VSTPluginMain"));
    CHECK(entry != nullptr, "no VSTPluginMain export");
    CHECK(GetProcAddress(dll, "main") != nullptr, "no legacy main export");
    if (!entry) return 1;

    if (argc > 3 && std::string(argv[3]) == "--defaults-shot") {
        // Fresh instance, default settings, no audio: the picture to compare with the JUCE editor
        AEffect* e = entry(hostCb);
        call(e, effOpen);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"aldj_shot";
        RegisterClassW(&wc);
        HWND top = CreateWindowW(L"aldj_shot", L"shot", WS_POPUP | WS_VISIBLE, 0, 0, 840, 700, nullptr, nullptr, wc.hInstance, nullptr);
        call(e, effEditOpen, 0, 0, top);
        pump(400);
        saveBmp(GetWindow(top, GW_CHILD), shots + "_defaults.bmp");
        call(e, effEditClose);
        call(e, effClose);
        return 0;
    }
    AEffect* fx = entry(hostCb);
    CHECK(fx && fx->magic == kMagic, "bad AEffect");
    CHECK(fx->numParams == aldj::kNumParams && fx->numInputs == 2 && fx->numOutputs == 2, "bad channel/param counts");
    CHECK((fx->flags & effFlagsHasEditor) && (fx->flags & effFlagsCanReplacing) && (fx->flags & effFlagsProgramChunks), "flags 0x%x", fx->flags);
    CHECK(fx->uniqueID == (('A' << 24) | ('l' << 16) | ('D' << 8) | 'j'), "unique id");
    char name[256] = {};
    call(fx, effGetEffectName, 0, 0, name);
    CHECK(std::string(name) == "AutoLevel DJ", "effect name '%s'", name);

    // ---- 3. parameters
    for (int i = 0; i < fx->numParams; ++i) {
        char pn[64] = {}, disp[64] = {};
        call(fx, effGetParamName, i, 0, pn);
        CHECK(std::string(pn) == aldj::def(i).name, "param %d name '%s'", i, pn);
        // defaults: the box's (a fresh AudioEngine), as plugin_test checks for the JUCE build
        aldj::Engine fresh;
        const float engineDefault = fresh.getParam(static_cast<aldj::Param>(i));
        const float hostDefault = aldj::fromNormalised(i, fx->getParameter(fx, i));
        CHECK(aldj::toEngineValue(i, hostDefault) == engineDefault, "param %d default %g vs engine %g", i, hostDefault, engineDefault);
        // every legal step survives the normalised round trip a host's automation takes
        const auto& d = aldj::def(i);
        for (double v = d.lo; v <= d.hi + 1e-9; v += d.step) {
            const float hv = aldj::snap(d, static_cast<float>(v));
            const float back = aldj::fromNormalised(i, aldj::toNormalised(i, hv));
            if (back != hv) { CHECK(false, "param %d value %g -> %g after round trip", i, hv, back); break; }
        }
        call(fx, effGetParamDisplay, i, 0, disp);
        CHECK(std::strlen(disp) > 0, "param %d empty display", i);
    }
    {
        char disp[64] = {};
        call(fx, effGetParamDisplay, static_cast<int>(aldj::Param::HPF_FREQ), 0, disp);
        CHECK(std::string(disp) == "30 Hz", "low cut display '%s'", disp);
        call(fx, effGetParamDisplay, static_cast<int>(aldj::Param::POST_MBC_GAIN), 0, disp);
        CHECK(std::string(disp) == "0.0 dB", "post gain display '%s'", disp);
        call(fx, effGetParamDisplay, static_cast<int>(aldj::Param::LEVEL_RESPONSE), 0, disp);
        CHECK(std::string(disp) == "85%", "level response display '%s'", disp);
        call(fx, effGetParamDisplay, static_cast<int>(aldj::Param::MBC_DETECTOR_RMS), 0, disp);
        CHECK(std::string(disp) == "Peak", "detector display '%s'", disp);
        fx->setParameter(fx, static_cast<int>(aldj::Param::HPF_FREQ), 0.0f);
        call(fx, effGetParamDisplay, static_cast<int>(aldj::Param::HPF_FREQ), 0, disp);
        CHECK(std::string(disp) == "Off", "low cut at 0 display '%s'", disp);
    }

    // ---- 1. audio parity
    call(fx, effOpen);
    call(fx, effSetSampleRate, 0, 0, nullptr, 48000.0f);
    call(fx, effSetBlockSize, 0, 1024);
    // Non-default settings, set the way a host automates them (normalised values)
    struct Set { aldj::Param p; float v; } sets[] = {
        { aldj::Param::TARGET_LUFS, -11.5f }, { aldj::Param::COMPRESSION_AMOUNT, 0.73f }, { aldj::Param::TONE_SLOPE, -1.3f },
        { aldj::Param::MBC_ATTACK_MS, 7.5f }, { aldj::Param::MBC_RELEASE_MS, 120.0f }, { aldj::Param::MBC_DETECTOR_RMS, 0.4f },
        { aldj::Param::MBC_AUTO_RELEASE, 1.0f }, { aldj::Param::AIR_EXCITER, 2.0f }, { aldj::Param::HPF_FREQ, 35.0f },
        { aldj::Param::CEILING_DB, -1.0f }, { aldj::Param::POST_MBC_GAIN, 1.5f }, { aldj::Param::SLEW_SPEED, 2.0f },
        { aldj::Param::CUSTOM_BAND_1, 2.5f }, { aldj::Param::CUSTOM_BAND_4, -3.0f }, { aldj::Param::PRE_EQ_BAND_0, -2.0f },
        { aldj::Param::PRE_EQ_BAND_5, 1.7f }, { aldj::Param::BREAKDOWN_THRESHOLD_LU, 9.0f },
    };
    aldj::Engine ref;
    ref.prepare(48000.0);
    for (int i = 0; i < aldj::kNumParams; ++i) ref.setParam(static_cast<aldj::Param>(i), aldj::toEngineValue(i, aldj::def(i).def));
    for (const auto& s : sets) {
        const int i = static_cast<int>(s.p);
        fx->setParameter(fx, i, aldj::toNormalised(i, s.v));
        const float got = aldj::fromNormalised(i, fx->getParameter(fx, i));
        CHECK(got == s.v, "param %d set %g reads back %g", i, s.v, got);
        ref.setParam(s.p, aldj::toEngineValue(i, s.v));
    }
    call(fx, effMainsChanged, 0, 1);
    CHECK(fx->initialDelay == 256 + ref.latencyFrames(), "initialDelay %d", fx->initialDelay);

    const size_t N = 48000 * 12;
    std::vector<float> inL, inR;
    makeSignal(inL, inR, N);
    std::vector<float> outL(N), outR(N);
    {
        size_t pos = 0;
        const int sizes[] = { 512, 1, 333, 1024, 64, 4096, 7, 256, 960, 1500 };
        int k = 0;
        while (pos < N) {
            const size_t n = std::min<size_t>(static_cast<size_t>(sizes[k++ % 10]), N - pos);
            // the host's in-place processing: outputs alias the inputs on alternate blocks
            std::vector<float> bl(inL.begin() + pos, inL.begin() + pos + n), br(inR.begin() + pos, inR.begin() + pos + n);
            float* ins[2] = { bl.data(), br.data() };
            std::vector<float> ol(n), orr(n);
            float* outs[2] = { ol.data(), orr.data() };
            if (k % 2) fx->processReplacing(fx, ins, ins, static_cast<int32_t>(n));
            else fx->processReplacing(fx, ins, outs, static_cast<int32_t>(n));
            const float* rl = (k % 2) ? bl.data() : ol.data();
            const float* rr = (k % 2) ? br.data() : orr.data();
            std::memcpy(&outL[pos], rl, n * 4);
            std::memcpy(&outR[pos], rr, n * 4);
            pos += n;
        }
    }
    std::vector<float> refL(N, 0.0f), refR(N, 0.0f);
    for (size_t b = 0; b + 256 <= N; b += 256) {
        ref.process(&inL[b], &inR[b], &refL[b], &refR[b], 256);
    }
    size_t mismatches = 0;
    double maxDiff = 0.0, peak = 0.0;
    for (size_t i = 256; i < N; ++i) {
        const float a = outL[i], b = refL[i - 256], c = outR[i], d = refR[i - 256];
        if (std::memcmp(&a, &b, 4) != 0 || std::memcmp(&c, &d, 4) != 0) ++mismatches;
        maxDiff = std::max(maxDiff, static_cast<double>(std::max(std::fabs(a - b), std::fabs(c - d))));
        peak = std::max(peak, static_cast<double>(std::fabs(a)));
    }
    for (size_t i = 0; i < 256; ++i) CHECK(outL[i] == 0.0f && outR[i] == 0.0f, "first block not silent");
    CHECK(mismatches == 0, "%zu samples differ from the engine (max diff %g)", mismatches, maxDiff);
    CHECK(peak > 0.05, "output is silent (peak %g)", peak);
    std::printf("audio: %zu frames, peak %.3f, %zu mismatches vs AudioEngine in 256-frame blocks\n", N, peak, mismatches);
    {
        FILE* f = std::fopen("vst2_out.f32", "wb");
        if (f) {
            for (size_t i = 0; i < N; ++i) { std::fwrite(&outL[i], 4, 1, f); std::fwrite(&outR[i], 4, 1, f); }
            std::fclose(f);
        }
    }

    // ---- 2. state
    {
        void* data = nullptr;
        const intptr_t size = call(fx, effGetChunk, 0, 0, &data);
        CHECK(size > 0 && data != nullptr, "empty chunk");
        const std::string chunk(static_cast<const char*>(data), static_cast<size_t>(size));
        CHECK(chunk.find("\"target_lufs\":-11.5") != std::string::npos && chunk.find("\"custom_offsets\":[0.0,2.5,0.0,0.0,-3.0,0.0]") != std::string::npos,
              "chunk is not the box preset: %s", chunk.c_str());
        AEffect* fx2 = entry(hostCb);
        call(fx2, effOpen);
        call(fx2, effSetChunk, 0, size, const_cast<char*>(chunk.data()));
        for (int i = 0; i < aldj::kNumParams; ++i)
            CHECK(fx2->getParameter(fx2, i) == fx->getParameter(fx, i), "param %d not restored (%g vs %g)", i,
                  fx2->getParameter(fx2, i), fx->getParameter(fx, i));
        call(fx2, effClose);
    }

    // ---- 4. editor
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"aldj_testhost";
    RegisterClassW(&wc);
    ERect* er = nullptr;
    call(fx, effEditGetRect, 0, 0, &er);
    CHECK(er && er->right - er->left == 840 && er->bottom - er->top == 700, "editor rect");
    RECT wr{ 0, 0, 840, 700 };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND top = CreateWindowW(L"aldj_testhost", L"AutoLevel DJ test host", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                             wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(call(fx, effEditOpen, 0, 0, top) == 1, "effEditOpen failed");
    HWND ed = GetWindow(top, GW_CHILD);
    CHECK(ed != nullptr, "no editor window");
    if (!ed) return 1;

    // play some audio so the meters move
    auto play = [&](size_t from, size_t frames) {
        for (size_t p = from; p + 512 <= from + frames && p + 512 <= N; p += 512) {
            std::vector<float> bl(inL.begin() + p, inL.begin() + p + 512), br(inR.begin() + p, inR.begin() + p + 512);
            float* io[2] = { bl.data(), br.data() };
            fx->processReplacing(fx, io, io, 512);
        }
    };
    play(0, 48000 * 8);
    pump(400);
    if (!shots.empty()) saveBmp(ed, shots + "_main.bmp");

    auto P = [&](aldj::Param p) { return aldj::fromNormalised(static_cast<int>(p), fx->getParameter(fx, static_cast<int>(p))); };
    auto click = [&](int x, int y) {
        SendMessageW(ed, WM_MOUSEMOVE, 0, xy(x, y));
        SendMessageW(ed, WM_LBUTTONDOWN, MK_LBUTTON, xy(x, y));
        SendMessageW(ed, WM_LBUTTONUP, 0, xy(x, y));
        pump(30);
    };

    // knob drag: Target LUFS (col 0, row 1; dial centre at 78, 502). Up 50 px = +0.2 of the travel
    {
        const float before = P(aldj::Param::TARGET_LUFS);
        g_automated.clear();
        SendMessageW(ed, WM_LBUTTONDOWN, MK_LBUTTON, xy(78, 502));
        for (int d = 5; d <= 50; d += 5) SendMessageW(ed, WM_MOUSEMOVE, MK_LBUTTON, xy(78, 502 - d));
        SendMessageW(ed, WM_LBUTTONUP, 0, xy(78, 452));
        const float expect = aldj::fromNormalised(0, aldj::toNormalised(0, before) + 0.2f);
        CHECK(P(aldj::Param::TARGET_LUFS) == expect, "knob drag: %g -> %g, expected %g", before, P(aldj::Param::TARGET_LUFS), expect);
        CHECK(!g_automated.empty(), "knob drag did not automate");
    }
    // Air Lift HIGH button (759..810, 94..116)
    click(784, 105);
    CHECK(P(aldj::Param::AIR_EXCITER) == 3.0f, "Air Lift HIGH -> %g", P(aldj::Param::AIR_EXCITER));
    // Slew SLOW
    click(368, 212);
    CHECK(P(aldj::Param::SLEW_SPEED) == 0.0f, "slew SLOW -> %g", P(aldj::Param::SLEW_SPEED));
    // Release AUTO -> MANUAL
    click(488, 252);
    CHECK(P(aldj::Param::MBC_AUTO_RELEASE) == 0.0f, "release MANUAL -> %g", P(aldj::Param::MBC_AUTO_RELEASE));
    // Breakdown 5LU
    click(402, 187);
    CHECK(P(aldj::Param::BREAKDOWN_THRESHOLD_LU) == 5.0f, "5LU -> %g", P(aldj::Param::BREAKDOWN_THRESHOLD_LU));
    // Bypass toggle on and off
    click(772, 27);
    CHECK(P(aldj::Param::BYPASS) == 1.0f, "bypass on");
    click(772, 27);
    CHECK(P(aldj::Param::BYPASS) == 0.0f, "bypass off");
    // Freeze off -> breakdown buttons ignore clicks
    click(331, 187);
    CHECK(P(aldj::Param::FREEZE_BREAKDOWNS) == 0.0f, "freeze off");
    click(442, 187);
    CHECK(P(aldj::Param::BREAKDOWN_THRESHOLD_LU) == 5.0f, "9LU clicked while freeze off");
    click(331, 187);

    // Faders: PRE-EQ mode, click the top of the Sub fader's travel -> +12 dB on EQ Sub
    click(764, 252);
    {
        const int fx0 = 26 + 42 + 62, fy0 = 266 + 22; // Sub fader origin
        click(fx0 + 25, fy0 + 12);
        CHECK(P(aldj::Param::PRE_EQ_BAND_0) == 12.0f, "EQ Sub fader top -> %g", P(aldj::Param::PRE_EQ_BAND_0));
        // double-click resets it
        SendMessageW(ed, WM_LBUTTONDOWN, MK_LBUTTON, xy(fx0 + 25, fy0 + 40));
        SendMessageW(ed, WM_LBUTTONUP, 0, xy(fx0 + 25, fy0 + 40));
        SendMessageW(ed, WM_LBUTTONDBLCLK, MK_LBUTTON, xy(fx0 + 25, fy0 + 40));
        SendMessageW(ed, WM_LBUTTONUP, 0, xy(fx0 + 25, fy0 + 40));
        CHECK(P(aldj::Param::PRE_EQ_BAND_0) == 0.0f, "EQ Sub double-click -> %g", P(aldj::Param::PRE_EQ_BAND_0));
        // Bass fader to 3/4 of the travel: +6 dB
        const int bx0 = 26 + 42 + 123 + 62;
        click(bx0 + 25, fy0 + 12 + 24);
        CHECK(P(aldj::Param::PRE_EQ_BAND_1) == 6.0f, "EQ Bass fader -> %g", P(aldj::Param::PRE_EQ_BAND_1));
    }
    if (!shots.empty()) { pump(100); saveBmp(ed, shots + "_eq.bmp"); }
    click(686, 252); // back to CONTOUR

    // Typing a value: click Max Boost's value box (col 1 row 1: x 147..239, box y 542..564)
    {
        click(193, 553);
        HWND edit = GetWindow(ed, GW_CHILD);
        CHECK(edit != nullptr, "value box did not open an edit field");
        if (edit) {
            if (!shots.empty()) { SetWindowTextW(edit, L"9.5"); pump(60); saveBmp(ed, shots + "_typing.bmp"); }
            SetWindowTextW(edit, L"9.5 dB");
            SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            pump(60);
            CHECK(P(aldj::Param::MAX_BOOST) == 9.5f, "typed Max Boost -> %g", P(aldj::Param::MAX_BOOST));
            CHECK(GetWindow(ed, GW_CHILD) == nullptr, "edit field still open after Enter");
        }
        // Escape cancels
        click(193, 553);
        edit = GetWindow(ed, GW_CHILD);
        if (edit) {
            SetWindowTextW(edit, L"3");
            SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
            pump(60);
            CHECK(P(aldj::Param::MAX_BOOST) == 9.5f, "Esc still applied: %g", P(aldj::Param::MAX_BOOST));
        }
        // percent setting: "60" means 60 %
        click(32 + 3 * 115 + 46, 553);
        edit = GetWindow(ed, GW_CHILD);
        if (edit) {
            SetWindowTextW(edit, L"60");
            SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            pump(60);
            CHECK(P(aldj::Param::LEVEL_RESPONSE) == 0.6f, "typed Level Response -> %g", P(aldj::Param::LEVEL_RESPONSE));
        }
    }
    // Mouse wheel on Compression (col 0 row 2): one notch up = +0.035 of the travel
    {
        const float before = P(aldj::Param::COMPRESSION_AMOUNT);
        POINT pt{ 78, 610 };
        ClientToScreen(ed, &pt);
        SendMessageW(ed, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
        const float expect = aldj::fromNormalised(5, aldj::toNormalised(5, before) + 0.5f * 120.0f / 256.0f * 0.15f);
        CHECK(P(aldj::Param::COMPRESSION_AMOUNT) == expect, "wheel: %g -> %g, expected %g", before, P(aldj::Param::COMPRESSION_AMOUNT), expect);
    }

    // Presets menu "Reset to Default" is a popup; reset directly through the chunk instead
    play(48000 * 8, 48000 * 3);
    pump(300);
    if (!shots.empty()) saveBmp(ed, shots + "_after.bmp");

    // Resize from the corner: the host allows it, so the window follows at the fixed aspect ratio
    {
        SetCursorPos(100, 100);
        POINT start;
        GetCursorPos(&start);
        SendMessageW(ed, WM_LBUTTONDOWN, MK_LBUTTON, xy(835, 695));
        SetCursorPos(start.x + 210, start.y);
        SendMessageW(ed, WM_MOUSEMOVE, MK_LBUTTON, xy(1045, 695));
        SendMessageW(ed, WM_LBUTTONUP, 0, xy(1045, 695));
        RECT rc;
        GetClientRect(ed, &rc);
        std::printf("resize: host asked for %d x %d, editor now %ld x %ld\n", g_sizeW, g_sizeH, rc.right, rc.bottom);
        if (g_sizeW == 1050) {
            CHECK(rc.right == 1050 && rc.bottom == 875, "editor not resized");
            SetWindowPos(top, nullptr, 0, 0, 1080, 920, SWP_NOMOVE);
            pump(200);
            if (!shots.empty()) saveBmp(ed, shots + "_large.bmp");
        }
    }

    call(fx, effEditClose);
    CHECK(GetWindow(top, GW_CHILD) == nullptr, "editor window left behind");
    // reopen and close again (GDI+ restarts cleanly)
    CHECK(call(fx, effEditOpen, 0, 0, top) == 1, "second effEditOpen failed");
    pump(100);
    call(fx, effEditClose);
    DestroyWindow(top);
    call(fx, effMainsChanged, 0, 0);
    call(fx, effClose);

    std::printf(g_failures == 0 ? "ALL VST2 TESTS PASSED\n" : "%d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
