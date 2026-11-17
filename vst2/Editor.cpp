/*
 * The JUCE editor (src/PluginEditor.cpp) ported to Win32 + GDI+. The paint code follows the
 * JUCE code statement by statement; the helpers below reproduce the JUCE calls it uses
 * (rounded rectangles, arcs, clamped gradients, top/centred text with ellipsis or squashing),
 * and the controls reproduce JUCE's Slider / Button / Label behaviour for this editor:
 *  - knobs: drag up or right to increase, 250 px for the full travel, mouse wheel 3.5 % a notch;
 *  - band faders: click or drag to the position, double-click for 0 dB;
 *  - value boxes under knobs and faders: click to type a value (Enter applies, Esc cancels);
 *  - buttons act on release, with hover and pressed states.
 */
#include "Editor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aldj {

namespace {

namespace G = Gdiplus;
using Bands = autolevel::dsp::Bands;

constexpr int W = 840, H = 700;
constexpr int MIN_W = 630, MAX_W = 1680;
constexpr int RESIZER = 18; // JUCE AudioProcessorEditor's corner resizer
constexpr UINT_PTR TIMER_ID = 1;
constexpr UINT WM_ALDJ_END_EDIT = WM_APP + 1;
constexpr double PI = 3.14159265358979323846;

// ------------------------------------------------------------------------------------------
// Colour, as juce::Colour does it

struct Col {
    uint32_t argb;
    uint8_t a() const { return static_cast<uint8_t>(argb >> 24); }
    uint8_t r() const { return static_cast<uint8_t>(argb >> 16); }
    uint8_t g() const { return static_cast<uint8_t>(argb >> 8); }
    uint8_t b() const { return static_cast<uint8_t>(argb); }
    static Col make(int a, int r, int g, int b) {
        return { (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b) };
    }
    static uint8_t toByte(float f) { return f <= 0.0f ? 0 : f >= 1.0f ? 255 : static_cast<uint8_t>(std::lround(f * 255.0f)); }
    Col withAlpha(float alpha) const { return make(toByte(alpha), r(), g(), b()); }
    Col brighter(float amount) const {
        amount = 1.0f / (1.0f + amount);
        return make(a(), static_cast<uint8_t>(255 - (amount * static_cast<float>(255 - r()))),
                    static_cast<uint8_t>(255 - (amount * static_cast<float>(255 - g()))),
                    static_cast<uint8_t>(255 - (amount * static_cast<float>(255 - b()))));
    }
    Col darker(float amount) const {
        amount = 1.0f / (1.0f + amount);
        return make(a(), static_cast<uint8_t>(amount * static_cast<float>(r())),
                    static_cast<uint8_t>(amount * static_cast<float>(g())),
                    static_cast<uint8_t>(amount * static_cast<float>(b())));
    }
    G::Color gdi() const { return G::Color(static_cast<G::ARGB>(argb)); }
    bool operator==(const Col& o) const { return argb == o.argb; }
};

constexpr Col C(uint32_t v) { return { v }; }
const Col WHITE = C(0xffffffff);
const Col CYAN = C(0xff00e5ff);
const Col AMBER = C(0xffffb300);
const Col GREEN = C(0xff00e676);
const Col RED = C(0xffff3366);

struct RectF {
    float x, y, w, h;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    RectF reduced(float d) const { return { x + d, y + d, w - 2 * d, h - 2 * d }; }
    RectF reduced(float dx, float dy) const { return { x + dx, y + dy, w - 2 * dx, h - 2 * dy }; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};
RectF R(int x, int y, int w, int h) { return { static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h) }; }

enum Just { LEFT_TOP, CENTRED, CENTRED_LEFT, CENTRED_RIGHT };

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string str1(float v) { return fmt1(v); }

// ------------------------------------------------------------------------------------------
// GDI+ lifetime: started with the first open editor, shut down with the last

int g_gdiplusUsers = 0;
ULONG_PTR g_gdiplusToken = 0;

bool acquireGdiplus() {
    if (g_gdiplusUsers++ == 0) {
        G::GdiplusStartupInput input;
        if (G::GdiplusStartup(&g_gdiplusToken, &input, nullptr) != G::Ok) {
            g_gdiplusUsers = 0;
            return false;
        }
    }
    return true;
}

void releaseGdiplus() {
    if (g_gdiplusUsers > 0 && --g_gdiplusUsers == 0) G::GdiplusShutdown(g_gdiplusToken);
}

// ------------------------------------------------------------------------------------------
// Fonts: JUCE's default sans-serif on Windows is Verdana; a JUCE font height is ascent + descent

class Fonts {
public:
    Fonts() {
        // ALDJ_FONT: a different family, for comparing renders on machines without Verdana
        wchar_t forced[64] = {};
        GetEnvironmentVariableW(L"ALDJ_FONT", forced, 64);
        for (const wchar_t* name : { static_cast<const wchar_t*>(forced), L"Verdana", L"Tahoma", L"Segoe UI", L"Arial", L"DejaVu Sans" }) {
            if (name[0] == 0) continue;
            auto* f = new G::FontFamily(name);
            if (f->GetLastStatus() == G::Ok && f->IsStyleAvailable(G::FontStyleBold)) { m_family = f; m_familyName = name; break; }
            delete f;
        }
        if (m_family == nullptr) m_family = G::FontFamily::GenericSansSerif()->Clone();
    }
    ~Fonts() {
        for (auto& kv : m_fonts) delete kv.second;
        delete m_family;
    }
    /** em size in pixels for a JUCE height of h. */
    float emFor(float h, bool bold) const {
        const int style = bold ? G::FontStyleBold : G::FontStyleRegular;
        const float em = static_cast<float>(m_family->GetEmHeight(style));
        const float cell = static_cast<float>(m_family->GetCellAscent(style) + m_family->GetCellDescent(style));
        return cell > 0.0f ? h * em / cell : h * 0.82f;
    }
    G::Font* get(float h, bool bold) {
        const int key = static_cast<int>(std::lround(h * 100.0f)) * 2 + (bold ? 1 : 0);
        auto it = m_fonts.find(key);
        if (it != m_fonts.end()) return it->second;
        auto* f = new G::Font(m_family, emFor(h, bold), bold ? G::FontStyleBold : G::FontStyleRegular, G::UnitPixel);
        m_fonts[key] = f;
        return f;
    }
    const std::wstring& familyName() const { return m_familyName; }

private:
    G::FontFamily* m_family = nullptr;
    std::wstring m_familyName = L"Verdana";
    std::map<int, G::Font*> m_fonts;
};

// ------------------------------------------------------------------------------------------
// The juce::Graphics calls the editor uses

class Canvas {
public:
    Canvas(G::Graphics& g, Fonts& fonts) : m_g(g), m_fonts(fonts) {
        m_format = G::StringFormat::GenericTypographic()->Clone();
        m_format->SetFormatFlags(m_format->GetFormatFlags() | G::StringFormatFlagsMeasureTrailingSpaces | G::StringFormatFlagsNoWrap);
    }
    ~Canvas() {
        delete m_format;
        delete m_brush;
    }

    G::Graphics& g() { return m_g; }

    void setColour(Col c) {
        delete m_brush;
        m_brush = new G::SolidBrush(c.gdi());
        m_colour = c;
    }

    /** A linear juce::ColourGradient: c1 at (x1, y1), c2 at (x2, y2), clamped beyond them. */
    void setGradient(Col c1, float x1, float y1, Col c2, float x2, float y2) {
        delete m_brush;
        m_colour = c1;
        float dx = x2 - x1, dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) { m_brush = new G::SolidBrush(c2.gdi()); return; }
        // GDI+ repeats a gradient beyond its end points, JUCE holds the end colours: stretch the
        // brush far beyond the shape and keep the colours flat outside the real span.
        const float ext = 20000.0f;
        dx /= len; dy /= len;
        G::PointF p1(x1 - dx * ext, y1 - dy * ext), p2(x2 + dx * ext, y2 + dy * ext);
        auto* b = new G::LinearGradientBrush(p1, p2, c1.gdi(), c2.gdi());
        const float total = len + 2.0f * ext;
        G::Color cols[4] = { c1.gdi(), c1.gdi(), c2.gdi(), c2.gdi() };
        G::REAL pos[4] = { 0.0f, ext / total, (ext + len) / total, 1.0f };
        b->SetInterpolationColors(cols, pos, 4);
        b->SetWrapMode(G::WrapModeTileFlipXY);
        m_brush = b;
    }

    void fillAll(Col c, float w, float h) { setColour(c); fillRect(0, 0, w, h); }

    void fillRect(float x, float y, float w, float h) {
        if (w <= 0 || h <= 0) return;
        m_g.FillRectangle(m_brush, x, y, w, h);
    }
    void fillRect(RectF r) { fillRect(r.x, r.y, r.w, r.h); }

    void hLine(int y, float left, float right) { if (right > left) fillRect(left, static_cast<float>(y), right - left, 1.0f); }
    void vLine(int x, float top, float bottom) { if (bottom > top) fillRect(static_cast<float>(x), top, 1.0f, bottom - top); }

    /** juce::Graphics::drawRect(Rectangle<int>): a 1 px frame inside the rectangle. */
    void drawRectInside(RectF r) {
        fillRect(r.x, r.y, r.w, 1);
        fillRect(r.x, r.bottom() - 1, r.w, 1);
        fillRect(r.x, r.y + 1, 1, r.h - 2);
        fillRect(r.right() - 1, r.y + 1, 1, r.h - 2);
    }

    static void addRounded(G::GraphicsPath& p, RectF r, float cs) {
        cs = std::min({ cs, r.w * 0.5f, r.h * 0.5f });
        if (cs <= 0.01f) { p.AddRectangle(G::RectF(r.x, r.y, r.w, r.h)); return; }
        const float d = cs * 2.0f;
        p.AddArc(r.x, r.y, d, d, 180.0f, 90.0f);
        p.AddArc(r.right() - d, r.y, d, d, 270.0f, 90.0f);
        p.AddArc(r.right() - d, r.bottom() - d, d, d, 0.0f, 90.0f);
        p.AddArc(r.x, r.bottom() - d, d, d, 90.0f, 90.0f);
        p.CloseFigure();
    }

    void fillRounded(RectF r, float cs) {
        if (r.w <= 0 || r.h <= 0) return;
        G::GraphicsPath p;
        addRounded(p, r, cs);
        m_g.FillPath(m_brush, &p);
    }
    void fillRounded(float x, float y, float w, float h, float cs) { fillRounded(RectF{ x, y, w, h }, cs); }

    void drawRounded(RectF r, float cs, float thickness) {
        G::GraphicsPath p;
        addRounded(p, r, cs);
        G::Pen pen(m_colour.gdi(), thickness);
        m_g.DrawPath(&pen, &p);
    }

    void fillEllipse(float x, float y, float w, float h) { m_g.FillEllipse(m_brush, x, y, w, h); }
    void fillEllipse(RectF r) { fillEllipse(r.x, r.y, r.w, r.h); }
    void drawEllipse(RectF r, float thickness) {
        G::Pen pen(m_colour.gdi(), thickness);
        m_g.DrawEllipse(&pen, r.x, r.y, r.w, r.h);
    }

    /** strokePath with PathStrokeType(thickness, curved, rounded). */
    void strokePath(G::GraphicsPath& p, float thickness) {
        G::Pen pen(m_colour.gdi(), thickness);
        pen.SetLineJoin(G::LineJoinRound);
        pen.SetStartCap(G::LineCapRound);
        pen.SetEndCap(G::LineCapRound);
        m_g.DrawPath(&pen, &p);
    }
    void fillPath(G::GraphicsPath& p) { m_g.FillPath(m_brush, &p); }

    /** Path::addCentredArc: angles in radians, clockwise from 12 o'clock. */
    static void addCentredArc(G::GraphicsPath& p, float cx, float cy, float rad, float from, float to) {
        const float startDeg = static_cast<float>(from * 180.0 / PI) - 90.0f;
        const float sweepDeg = static_cast<float>((to - from) * 180.0 / PI);
        p.AddArc(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, startDeg, sweepDeg);
    }

    /** Advance width of s at a JUCE height, as DrawString will lay it out. */
    float textWidth(const std::wstring& s, float fontH, bool bold) {
        if (s.empty()) return 0.0f;
        G::RectF out;
        m_g.MeasureString(s.c_str(), static_cast<INT>(s.size()), m_fonts.get(fontH, bold), G::PointF(0, 0), m_format, &out);
        return out.Width;
    }

    /**
     * Graphics::drawText (minHScale 1: an ellipsis when too wide) or Graphics::drawFittedText
     * on one line (squashes down to minHScale, then the ellipsis).
     */
    void text(const std::string& s, RectF area, Just just, float fontH, bool bold, float minHScale = 1.0f) {
        if (s.empty()) return;
        std::wstring ws = widen(s);
        float tw = textWidth(ws, fontH, bold);
        float sx = 1.0f;
        if (tw > area.w && minHScale < 1.0f) sx = std::max(minHScale, area.w / tw);
        if (tw * sx > area.w + 0.01f) {
            while (!ws.empty()) {
                ws.pop_back();
                const std::wstring t = ws + L"...";
                if (textWidth(t, fontH, bold) * sx <= area.w || ws.empty()) { ws = t; break; }
            }
            tw = textWidth(ws, fontH, bold);
        }
        const float drawnW = tw * sx;
        float x = area.x, y = area.y;
        switch (just) {
            case LEFT_TOP: break;
            case CENTRED: x = area.x + (area.w - drawnW) * 0.5f; y = area.y + (area.h - fontH) * 0.5f; break;
            case CENTRED_LEFT: y = area.y + (area.h - fontH) * 0.5f; break;
            case CENTRED_RIGHT: x = area.right() - drawnW; y = area.y + (area.h - fontH) * 0.5f; break;
        }
        G::SolidBrush brush(m_colour.gdi());
        G::Font* f = m_fonts.get(fontH, bold);
        if (sx != 1.0f) {
            G::Matrix saved;
            m_g.GetTransform(&saved);
            m_g.TranslateTransform(x, y);
            m_g.ScaleTransform(sx, 1.0f);
            m_g.DrawString(ws.c_str(), static_cast<INT>(ws.size()), f, G::PointF(0, 0), m_format, &brush);
            m_g.SetTransform(&saved);
        } else {
            m_g.DrawString(ws.c_str(), static_cast<INT>(ws.size()), f, G::PointF(x, y), m_format, &brush);
        }
    }
    void text(const std::string& s, int x, int y, int w, int h, Just just, float fontH, bool bold) {
        text(s, R(x, y, w, h), just, fontH, bold);
    }

    /** A juce::Label: border 1/5/1/5, fitted text (squash to 70 %), optional fill and frame. */
    void label(const std::string& s, RectF area, Just just, float fontH, bool bold, Col textCol,
               const Col* background = nullptr, const Col* outline = nullptr) {
        if (background != nullptr) { setColour(*background); fillRect(area); }
        setColour(textCol);
        text(s, RectF{ area.x + 5, area.y + 1, area.w - 10, area.h - 2 }, just, fontH, bold, 0.7f);
        if (outline != nullptr) { setColour(*outline); drawRectInside(area); }
    }

private:
    G::Graphics& m_g;
    Fonts& m_fonts;
    G::StringFormat* m_format = nullptr;
    G::Brush* m_brush = nullptr;
    Col m_colour = WHITE;
};

// ------------------------------------------------------------------------------------------
// Controls

int idx(Param p) { return static_cast<int>(p); }

struct Knob {
    int param;
    const char* title;
    const char* suffix;
    Col accent;
    RectF labelArea;  // the title label above the knob
    RectF bounds;     // the juce::Slider, value box included
    RectF valueBox() const { return { bounds.x + (bounds.w - 76.0f) / 2.0f, bounds.bottom() - 22.0f, 76.0f, 22.0f }; }
    RectF dialArea() const { return { bounds.x, bounds.y, bounds.w, bounds.h - 22.0f }; }
};

struct Fader {
    int band;
    RectF bounds;
    RectF valueBox() const { return { bounds.x + (bounds.w - 46.0f) / 2.0f, bounds.bottom() - 16.0f, 46.0f, 16.0f }; }
    // Thumb travel: the slider area above the value box, less the 12 px thumb radius each end.
    float regionStart() const { return bounds.y + 12.0f; }
    float regionSize() const { return bounds.h - 16.0f - 24.0f; }
};

struct Button {
    std::string text;
    RectF bounds;
    bool isToggle = false;          // a juce::ToggleButton (LED style), else a TextButton
    Col colour = C(0xff181d26);     // TextButton buttonColourId
    Col textOff = C(0xffa6adb9);    // TextButton textColourOffId / ToggleButton textColourId
    std::function<bool()> toggled;
    std::function<bool()> enabled;
    std::function<void()> onClick;
};

struct Label {
    std::string text;
    RectF bounds;
    float fontH;
    Col colour;
    Just just;
};

enum class DragKind { None, Knob, Fader, Button, ValueBox, Resizer };

} // namespace

// ------------------------------------------------------------------------------------------

struct Editor::Impl {
    Plugin& plugin;
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    WNDPROC editProc = nullptr;
    int editParam = -1;
    HFONT editFont = nullptr;
    HBRUSH editBrush = nullptr;
    std::unique_ptr<Fonts> fonts;
    float scale = 1.0f;
    bool canResize = false;

    int memW = 0, memH = 0;

    std::vector<Knob> knobs;
    std::vector<Fader> faders;
    std::vector<Button> buttons;
    std::vector<Label> labels;
    bool fadersEditEq = false;

    // meters (MultibandMeterRack state)
    std::array<float, Bands::COUNT> currentGr{}, peakGr{}, contour{};
    std::array<int, Bands::COUNT> peakHold{};
    autolevel::dsp::EngineVisualState latest{};
    float maxHeldLimiterGrDb = 0.0f;

    // mouse
    int hoverButton = -1;
    int pressedButton = -1;
    bool pressedInside = false;
    DragKind drag = DragKind::None;
    int dragIndex = -1;
    int dragParam = -1;
    float dragStartX = 0, dragStartY = 0;
    float dragStartProp = 0;
    bool dragMoved = false;
    POINT resizeStart{};
    int resizeStartW = W;
    bool tracking = false;

    explicit Impl(Plugin& p) : plugin(p) { buildControls(); }

    int faderParam(int band) const {
        return idx(fadersEditEq ? Param::PRE_EQ_BAND_0 : Param::CUSTOM_BAND_0) + band;
    }
    float value(Param p) const { return plugin.hostValue(p); }

    int pixelW() const { return static_cast<int>(std::lround(W * scale)); }
    int pixelH() const { return static_cast<int>(std::lround(H * scale)); }

    void buildControls() {
        // Knobs, in the JUCE editor's setupRotary order (also their paint order)
        auto knob = [this](Param p, const char* title, const char* suffix, Col accent) {
            knobs.push_back({ idx(p), title, suffix, accent, {}, {} });
        };
        knob(Param::TARGET_LUFS, "TARGET LUFS", " LUFS", GREEN);
        knob(Param::COMPRESSION_AMOUNT, "COMPRESSION", "", CYAN);
        knob(Param::LEVEL_RESPONSE, "LEVEL RESPONSE", "", CYAN);
        knob(Param::TONE_SLOPE, "TONE SLOPE", " dB/oct", AMBER);
        knob(Param::CEILING_DB, "LIMITER CEILING", " dBFS", RED);
        knob(Param::MBC_DETECTOR_RMS, "DETECTOR", "", CYAN);
        knob(Param::MBC_ATTACK_MS, "MBC ATTACK", "", CYAN);
        knob(Param::MBC_RELEASE_MS, "MBC RELEASE", "", AMBER);
        knob(Param::MAX_BOOST, "MAX BOOST", " dB", CYAN);
        knob(Param::MAX_CUT, "MAX CUT", " dB", AMBER);
        knob(Param::POST_MBC_GAIN, "POST GAIN", "", CYAN);
        knob(Param::HPF_FREQ, "LOW CUT", "", AMBER);

        // layoutContent(): 6 columns x 2 rows
        auto place = [this](Param p, int col, int rowY) {
            for (auto& k : knobs)
                if (k.param == idx(p)) {
                    const int cx = 32 + col * 115;
                    k.labelArea = R(cx, rowY, 92, 14);
                    k.bounds = R(cx, rowY + 14, 92, 82);
                }
        };
        place(Param::TARGET_LUFS, 0, 468);
        place(Param::MAX_BOOST, 1, 468);
        place(Param::MAX_CUT, 2, 468);
        place(Param::LEVEL_RESPONSE, 3, 468);
        place(Param::POST_MBC_GAIN, 4, 468);
        place(Param::HPF_FREQ, 5, 468);
        place(Param::COMPRESSION_AMOUNT, 0, 576);
        place(Param::TONE_SLOPE, 1, 576);
        place(Param::MBC_DETECTOR_RMS, 2, 576);
        place(Param::MBC_ATTACK_MS, 3, 576);
        place(Param::MBC_RELEASE_MS, 4, 576);
        place(Param::CEILING_DB, 5, 576);

        const Col labelCol = C(0xff8b95a5);
        labels.push_back({ "SLEW:", R(306, 202, 40, 20), 10.0f, labelCol, CENTRED_RIGHT });

        auto choiceButtons = [this](Param p, std::vector<std::pair<const char*, RectF>> items) {
            for (size_t i = 0; i < items.size(); ++i) {
                Button b;
                b.text = items[i].first;
                b.bounds = items[i].second;
                const int param = idx(p);
                const float v = static_cast<float>(i);
                b.toggled = [this, param, v] { return std::lround(plugin.hostValue(param)) == std::lround(v); };
                b.onClick = [this, param, v] { plugin.changeFromEditor(param, v); };
                buttons.push_back(std::move(b));
            }
        };
        choiceButtons(Param::SLEW_SPEED, { { "SLOW", R(349, 202, 38, 20) }, { "NORMAL", R(390, 202, 44, 20) }, { "FAST", R(437, 202, 38, 20) } });

        {
            Button b;
            b.text = "Freeze";
            b.bounds = R(282, 176, 98, 22);
            b.isToggle = true;
            b.textOff = AMBER;
            const int param = idx(Param::FREEZE_BREAKDOWNS);
            b.toggled = [this, param] { return plugin.hostValue(param) >= 0.5f; };
            b.onClick = [this, param] { plugin.changeFromEditor(param, plugin.hostValue(param) >= 0.5f ? 0.0f : 1.0f); };
            buttons.push_back(std::move(b));
        }
        {
            Button b;
            b.text = "BYPASS";
            b.bounds = R(W - 116, 13, 96, 28);
            b.isToggle = true;
            b.textOff = RED;
            const int param = idx(Param::BYPASS);
            b.toggled = [this, param] { return plugin.hostValue(param) >= 0.5f; };
            b.onClick = [this, param] { plugin.changeFromEditor(param, plugin.hostValue(param) >= 0.5f ? 0.0f : 1.0f); };
            buttons.push_back(std::move(b));
        }
        {
            Button b;
            b.text = "RESET SET / INTEGRATION";
            b.bounds = R(W - 326, 13, 200, 28);
            b.textOff = CYAN;
            b.onClick = [this] { plugin.resetIntegration(); };
            buttons.push_back(std::move(b));
        }
        {
            Button b;
            b.text = "PRESETS";
            b.bounds = R(W - 432, 13, 96, 28);
            b.textOff = GREEN;
            b.onClick = [this] { showPresetsMenu(); };
            buttons.push_back(std::move(b));
        }

        labels.push_back({ "AIR LIFT:", R(526, 94, 64, 22), 10.0f, labelCol, CENTRED_RIGHT });
        choiceButtons(Param::AIR_EXCITER, { { "OFF", R(594, 94, 51, 22) }, { "LOW", R(649, 94, 51, 22) },
                                            { "MED", R(704, 94, 51, 22) }, { "HIGH", R(759, 94, 51, 22) } });

        // Breakdown sensitivity: Sensitive 5 LU / Normal 7 LU / Deep 9 LU, live only while freeze is on
        {
            const float values[3] = { 5.0f, 7.0f, 9.0f };
            const char* names[3] = { "5LU", "7LU", "9LU" };
            for (int i = 0; i < 3; ++i) {
                Button b;
                b.text = names[i];
                b.bounds = R(384 + i * 40, 177, 37, 20);
                const float v = values[i];
                const int param = idx(Param::BREAKDOWN_THRESHOLD_LU);
                b.toggled = [this, param, v] { return std::abs(plugin.hostValue(param) - v) < 0.25f; };
                b.enabled = [this] { return plugin.hostValue(Param::FREEZE_BREAKDOWNS) >= 0.5f; };
                b.onClick = [this, param, v] { plugin.changeFromEditor(param, v); };
                buttons.push_back(std::move(b));
            }
        }

        labels.push_back({ "FADERS:", R(594, 242, 52, 20), 10.0f, labelCol, CENTRED_RIGHT });
        {
            Button b;
            b.text = "CONTOUR";
            b.bounds = R(650, 242, 74, 20);
            b.toggled = [this] { return !fadersEditEq; };
            b.onClick = [this] { fadersEditEq = false; };
            buttons.push_back(std::move(b));
            Button e;
            e.text = "PRE-EQ";
            e.bounds = R(728, 242, 72, 20);
            e.toggled = [this] { return fadersEditEq; };
            e.onClick = [this] { fadersEditEq = true; };
            buttons.push_back(std::move(e));
        }

        labels.push_back({ "RELEASE:", R(392, 242, 60, 20), 10.0f, labelCol, CENTRED_RIGHT });
        choiceButtons(Param::MBC_AUTO_RELEASE, { { "MANUAL", R(456, 242, 64, 20) }, { "AUTO", R(524, 242, 56, 20) } });

        // Band faders, one beside each band's GR meter
        for (int b = 0; b < static_cast<int>(Bands::COUNT); ++b) {
            const float colX = rackColumnX(static_cast<size_t>(b), 788.0f);
            faders.push_back({ b, R(26 + static_cast<int>(colX) + 62, 266 + 22, 50, 136) });
        }
    }

    // MultibandMeterRack layout
    static constexpr float SCALE_W = 34.0f;
    static float rackColumnWidth(float rackWidth) { return (rackWidth - SCALE_W - 16.0f) / static_cast<float>(Bands::COUNT); }
    static float rackColumnX(size_t band, float rackWidth) { return SCALE_W + 8.0f + static_cast<float>(band) * rackColumnWidth(rackWidth); }

    // --------------------------------------------------------------------------------------
    // Timer: the JUCE editor's timerCallback at 60 Hz

    void tick() {
        latest = plugin.visualState();
        const float curLimGr = latest.limiterGainReductionDb;
        if (curLimGr < maxHeldLimiterGrDb) maxHeldLimiterGrDb = curLimGr;

        for (size_t b = 0; b < Bands::COUNT; ++b)
            contour[b] = value(static_cast<Param>(idx(Param::CUSTOM_BAND_0) + static_cast<int>(b)));

        // MultibandMeterRack::updateMeters
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            const float gr = latest.mbcGainReductionsDb[b];
            currentGr[b] = gr;
            if (gr < peakGr[b]) {
                peakGr[b] = gr;
                peakHold[b] = 36; // ~600 ms hold at 60 Hz
            } else if (peakHold[b] > 0) {
                --peakHold[b];
            } else {
                peakGr[b] = std::min(0.0f, peakGr[b] + 0.18f);
            }
        }
        // Repaint only when something on screen can have changed.
        const std::vector<float> sig = frameSignature();
        if (sig != lastFrameSig) {
            lastFrameSig = sig;
            redraw();
        }
    }

    /** Everything the meters and readouts show; equal signatures draw identical frames. */
    std::vector<float> frameSignature() const {
        std::vector<float> v;
        const auto& L = latest;
        for (float f : { L.loudness.integratedLUFS, L.loudness.momentaryLUFS, L.loudness.shortTermLUFS, L.appliedGainDb,
                         L.targetGainDb, L.activeHalfLifeSeconds, L.limiterGainReductionDb, L.outputPeakDbL,
                         L.outputPeakDbR, L.airLiftDb, maxHeldLimiterGrDb })
            v.push_back(f);
        v.insert(v.end(), currentGr.begin(), currentGr.end());
        v.insert(v.end(), peakGr.begin(), peakGr.end());
        const std::vector<float> c = controlsSignature();
        v.insert(v.end(), c.begin(), c.end());
        return v;
    }

    /** Everything the controls layer depends on. */
    std::vector<float> controlsSignature() const {
        std::vector<float> v;
        for (int i = 0; i < kNumParams; ++i) v.push_back(plugin.hostValue(i));
        for (float f : { fadersEditEq ? 1.0f : 0.0f, static_cast<float>(hoverButton), static_cast<float>(pressedButton),
                         pressedInside ? 1.0f : 0.0f, static_cast<float>(editParam), scale })
            v.push_back(f);
        return v;
    }

    // --------------------------------------------------------------------------------------
    // Painting, in three layers that never overlap where it matters, so drawing them one after
    // the other gives the JUCE editor's picture:
    //   STATIC   - chassis, cards, titles, scales, troughs: drawn once per size;
    //   CONTROLS - knobs, buttons, faders, tone curve: redrawn when a setting or hover changes;
    //   METERS   - readouts and meters: redrawn each frame while they move.

    enum Layer { STATIC, CONTROLS, METERS };

    void setupGraphics(G::Graphics& gr) const {
        gr.SetSmoothingMode(G::SmoothingModeAntiAlias);
        gr.SetPixelOffsetMode(G::PixelOffsetModeHalf);
        gr.SetTextRenderingHint(G::TextRenderingHintAntiAlias);
        gr.SetCompositingQuality(G::CompositingQualityHighSpeed);
        gr.ResetTransform();
        gr.ScaleTransform(scale, scale);
    }

    void paintLayer(G::Graphics& gr, Layer layer) {
        setupGraphics(gr);
        Canvas c(gr, *fonts);
        if (layer == STATIC) {
            paintContent(c, STATIC);
            paintMeterRack(c, R(26, 266, 788, 160), STATIC);
            for (const auto& l : labels) c.label(l.text, l.bounds, l.just, l.fontH, true, l.colour);
            for (const auto& k : knobs) c.label(k.title, k.labelArea, CENTRED, 11.0f, true, C(0xff9aa5b4));
            if (canResize) paintResizer(gr);
        } else if (layer == CONTROLS) {
            paintToneCurve(c, R(530, 138, 280, 80));
            for (const auto& k : knobs) paintKnob(c, k);
            for (size_t i = 0; i < buttons.size(); ++i) paintButton(c, buttons[i], static_cast<int>(i));
            for (const auto& f : faders) paintFader(c, f);
        } else {
            paintContent(c, METERS);
            paintMeterRack(c, R(26, 266, 788, 160), METERS);
            const int airIdx = static_cast<int>(std::lround(value(Param::AIR_EXCITER)));
            if (airIdx <= 0) c.label("Air Lift off", R(524, 118, 292, 16), CENTRED, 10.0f, true, C(0xff556070));
            else c.label("Air Lift: +" + str1(latest.airLiftDb) + " dB above 6.5 kHz", R(524, 118, 292, 16), CENTRED, 10.0f, true, CYAN);
        }
    }

    /** The editor's corner resizer (LookAndFeel_V2::drawCornerResizer), in window pixels. */
    void paintResizer(G::Graphics& gr) {
        gr.ResetTransform();
        const float cw = static_cast<float>(RESIZER);
        const float ox = static_cast<float>(pixelW()) - cw, oy = static_cast<float>(pixelH()) - cw;
        const float lt = cw * 0.075f;
        for (float i = 0.0f; i < 1.0f; i += 0.3f) {
            G::Pen light(G::Color(0xffd3d3d3), lt), dark(G::Color(0xffa9a9a9), lt);
            gr.DrawLine(&light, ox + cw * i, oy + cw + 1.0f, ox + cw + 1.0f, oy + cw * i);
            gr.DrawLine(&dark, ox + cw * i + lt, oy + cw + 1.0f, ox + cw + 1.0f, oy + cw * i + lt);
        }
        gr.ScaleTransform(scale, scale);
    }

    void paintKnob(Canvas& c, const Knob& k) {
        const float v = plugin.hostValue(k.param);
        const float prop = toNormalised(k.param, v);
        // ModernHardwareLookAndFeel::drawRotarySlider on the slider area above the value box
        const RectF a = k.dialArea();
        const float textBoxH = 22.0f;
        const float diameter = std::min(a.w, a.h - textBoxH) - 14.0f;
        if (diameter >= 10.0f) {
            const float startA = static_cast<float>(PI * 1.2), endA = static_cast<float>(PI * 2.8);
            const float radius = diameter * 0.5f;
            const float cx = a.x + a.w * 0.5f;
            const float cy = a.y + (a.h - textBoxH) * 0.5f + 1.0f;
            const float arcR = radius + 4.0f;

            {
                G::GraphicsPath bg;
                Canvas::addCentredArc(bg, cx, cy, arcR, startA, endA);
                c.setColour(C(0xff1b212b));
                c.strokePath(bg, 4.0f);
            }
            const float cur = startA + prop * (endA - startA);
            if (cur > startA + 0.01f) {
                G::GraphicsPath val;
                Canvas::addCentredArc(val, cx, cy, arcR, startA, cur);
                c.setColour(k.accent.withAlpha(0.25f));
                c.strokePath(val, 7.0f);
                c.setColour(k.accent);
                c.strokePath(val, 4.0f);
            }
            const RectF dial{ cx - radius, cy - radius, diameter, diameter };
            c.setColour(C(0xff2d3646));
            c.drawEllipse(dial, 1.5f);
            c.setGradient(C(0xff222935), cx, cy - radius, C(0xff12151c), cx, cy + radius);
            c.fillEllipse(dial.reduced(1.0f));
            c.setColour(C(0xff161a22));
            c.fillEllipse(dial.reduced(radius * 0.40f));
            c.setColour(C(0xff262e3c));
            c.drawEllipse(dial.reduced(radius * 0.40f), 1.0f);

            const float l1 = radius * 0.42f, l2 = radius * 0.88f;
            const float sx = std::sin(cur), sy = -std::cos(cur);
            G::GraphicsPath needle;
            needle.AddLine(cx + l1 * sx, cy + l1 * sy, cx + l2 * sx, cy + l2 * sy);
            c.setColour(WHITE);
            c.strokePath(needle, 2.5f);
        }
        paintValueBox(c, k.valueBox(), k.param, valueText(k.param, v) + k.suffix, true);
    }

    /**
     * A slider's value box. The faders' boxes use the editor's look (11.5 px bold on a dark fill).
     * The knobs' boxes come out in JUCE's default look in the JUCE editor - each knob is
     * recoloured before it joins the editor, which rebuilds its box with the default
     * LookAndFeel_V4 - so they are drawn that way here too: 15 px regular, no fill, grey frame.
     */
    void paintValueBox(Canvas& c, RectF box, int param, const std::string& text, bool knobStyle = false) {
        const Col bg = C(0xff12161f), outline = knobStyle ? C(0xff8e989b) : C(0xff222836);
        if (edit != nullptr && editParam == param) {
            c.setColour(bg);
            c.fillRect(box);
            c.setColour(outline);
            c.drawRectInside(box);
            return;
        }
        if (knobStyle) c.label(text, box, CENTRED, 15.0f, false, WHITE, nullptr, &outline);
        else c.label(text, box, CENTRED, 11.5f, true, WHITE, &bg, &outline);
    }

    void paintFader(Canvas& c, const Fader& f) {
        const int param = faderParam(f.band);
        const float v = plugin.hostValue(param);
        const Col track = fadersEditEq ? GREEN : CYAN;
        const float prop = toNormalised(param, v);
        auto posOf = [&f](float p) { return f.regionStart() + (1.0f - p) * f.regionSize(); };
        const float sliderPos = posOf(prop);

        // ModernHardwareLookAndFeel::drawLinearSlider (vertical, bipolar)
        const float cx = f.bounds.x + f.bounds.w * 0.5f;
        const float bottomY = posOf(0.0f), topY = posOf(1.0f), zeroY = posOf(toNormalised(param, 0.0f));
        const RectF groove{ cx - 2.5f, topY, 5.0f, bottomY - topY };
        c.setColour(C(0xff0a0c10));
        c.fillRounded(groove, 2.5f);
        c.setColour(C(0xff1c222e));
        c.drawRounded(groove, 2.5f, 1.0f);
        for (int i = 0; i <= 4; ++i) {
            const float ty = topY + (bottomY - topY) * (static_cast<float>(i) / 4.0f);
            c.setColour(C(0xff252e3e));
            c.hLine(static_cast<int>(std::round(ty)), cx - 11.0f, cx - 6.0f);
            c.hLine(static_cast<int>(std::round(ty)), cx + 6.0f, cx + 11.0f);
        }
        c.setColour(C(0xff4a566a));
        c.hLine(static_cast<int>(std::round(zeroY)), cx - 13.0f, cx + 13.0f);

        const Col fill = v >= 0.0f ? track : AMBER;
        const float barTop = std::min(zeroY, sliderPos), barBottom = std::max(zeroY, sliderPos);
        if (barBottom - barTop > 0.5f) {
            c.setColour(fill.withAlpha(0.25f));
            c.fillRounded(cx - 4.5f, barTop, 9.0f, barBottom - barTop, 3.0f);
            c.setColour(fill);
            c.fillRounded(cx - 2.0f, barTop, 4.0f, barBottom - barTop, 2.0f);
        }
        const RectF thumb{ cx - 11.0f, sliderPos - 5.5f, 22.0f, 11.0f };
        c.setGradient(C(0xff2a3240), thumb.x, thumb.y, C(0xff151a22), thumb.x, thumb.bottom());
        c.fillRounded(thumb, 3.0f);
        c.setColour(C(0xff3b465a));
        c.drawRounded(thumb, 3.0f, 1.0f);
        c.setColour(WHITE);
        c.hLine(static_cast<int>(std::round(thumb.cy())), thumb.x + 4.0f, thumb.right() - 4.0f);

        paintValueBox(c, f.valueBox(), param, valueText(param, v));
    }

    void paintButton(Canvas& c, const Button& b, int index) {
        const bool on = b.toggled ? b.toggled() : false;
        const bool enabled = b.enabled ? b.enabled() : true;
        const bool down = enabled && pressedButton == index && pressedInside;
        const bool over = enabled && (hoverButton == index || down);

        if (b.isToggle) {
            // ModernHardwareLookAndFeel::drawToggleButton
            const RectF r = b.bounds.reduced(1.0f);
            Col accent = b.textOff;
            if (!on && accent == WHITE) accent = CYAN;
            Col bg = on ? C(0xff18202c) : C(0xff12151d);
            if (over) bg = bg.brighter(0.08f);
            c.setColour(bg);
            c.fillRounded(r, 5.0f);
            c.setColour(on ? accent : C(0xff252c3a));
            c.drawRounded(r, 5.0f, 1.2f);
            const float ledX = r.x + 14.0f, ledY = r.cy(), ledR = 3.5f;
            if (on) {
                c.setColour(accent.withAlpha(0.35f));
                c.fillEllipse(ledX - ledR - 2.0f, ledY - ledR - 2.0f, (ledR + 2.0f) * 2.0f, (ledR + 2.0f) * 2.0f);
                c.setColour(accent);
                c.fillEllipse(ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f);
            } else {
                c.setColour(C(0xff2a3240));
                c.fillEllipse(ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f);
            }
            c.setColour(on ? WHITE : C(0xff949da8));
            c.text(b.text, RectF{ r.x + 26.0f, r.y, r.w - 26.0f, r.h }, CENTRED_LEFT, 11.0f, true);
            return;
        }

        // drawButtonBackground + drawButtonText
        const RectF r = b.bounds.reduced(0.5f);
        Col bg = b.colour;
        if (on) bg = C(0xff182434);
        else if (down) bg = bg.darker(0.15f);
        else if (over) bg = bg.brighter(0.12f);
        c.setColour(bg);
        c.fillRounded(r, 5.0f);
        Col border = on ? CYAN : C(0xff2d3647);
        if (over && !on) border = border.brighter(0.2f);
        c.setColour(border);
        c.drawRounded(r, 5.0f, on ? 1.5f : 1.0f);
        c.setColour(on ? CYAN : b.textOff);
        c.text(b.text, b.bounds, CENTRED, 11.0f, true);
    }

    void paintToneCurve(Canvas& c, RectF area) {
        G::GraphicsState st = c.g().Save();
        c.g().TranslateTransform(area.x, area.y);
        const RectF bounds{ 0, 0, area.w, area.h };
        c.setColour(C(0xff0e1117));
        c.fillRounded(bounds, 6.0f);
        c.setColour(C(0xff222937));
        c.drawRounded(bounds, 6.0f, 1.0f);

        const RectF plot = bounds.reduced(8.0f, 14.0f);
        const float plotW = plot.w, plotH = plot.h, plotX = plot.x, plotY = plot.y;
        const float midY = plotY + plotH * 0.5f;
        c.setColour(C(0xff1c222e));
        c.hLine(static_cast<int>(midY), plotX, plotX + plotW);

        const auto& crossovers = Bands::CROSSOVERS;
        const float minLog = std::log10(20.0f), maxLog = std::log10(20000.0f);
        auto freqToX = [=](float freq) {
            const float norm = (std::log10(freq) - minLog) / (maxLog - minLog);
            return plotX + std::clamp(norm, 0.0f, 1.0f) * plotW;
        };
        for (size_t i = 0; i < crossovers.size(); ++i) {
            const float x = freqToX(crossovers[i]);
            c.setColour(C(0xff1c2330));
            c.vLine(static_cast<int>(x), plotY, plotY + plotH);
            c.setColour(C(0xff7c889a));
            const std::string fs = crossovers[i] >= 1000.0f ? str1(crossovers[i] / 1000.0f) + "k"
                                                            : std::to_string(static_cast<int>(crossovers[i]));
            c.text(fs, static_cast<int>(x) - 18, static_cast<int>(plotY + plotH) - 11, 36, 11, CENTRED, 9.5f, true);
        }
        for (size_t b = 0; b < Bands::COUNT; ++b) {
            const float x1 = b == 0 ? plotX : freqToX(crossovers[b - 1]);
            const float x2 = b == 5 ? plotX + plotW : freqToX(crossovers[b]);
            c.setColour(C(0xffb0bac8));
            c.text(std::string(Bands::NAMES[b]), static_cast<int>(x1), static_cast<int>(plotY) + 1,
                   static_cast<int>(x2 - x1), 12, CENTRED, 10.0f, true);
        }

        const float toneSlope = value(Param::TONE_SLOPE);
        auto contourAt = [this](float oct) {
            float sumW = 0.0f, sumVal = 0.0f;
            for (size_t b = 0; b < Bands::COUNT; ++b) {
                const float diff = oct - Bands::OCTAVES[b];
                const float w = std::exp(-0.5f * (diff * diff) / (0.8f * 0.8f));
                sumVal += w * contour[b];
                sumW += w;
            }
            return sumW > 1e-4f ? sumVal / sumW : 0.0f;
        };
        constexpr int STEPS = 64;
        std::vector<G::PointF> pts;
        for (int step = 0; step <= STEPS; ++step) {
            const float norm = static_cast<float>(step) / static_cast<float>(STEPS);
            const float f = std::pow(10.0f, minLog + norm * (maxLog - minLog));
            const float oct = static_cast<float>(std::log2(f));
            const float totalDb = toneSlope * (oct - Bands::MEAN_OCTAVE) + contourAt(oct);
            const float ny = 0.5f - totalDb / 20.0f;
            pts.emplace_back(plotX + norm * plotW, plotY + std::clamp(ny, 0.05f, 0.95f) * plotH);
        }
        G::GraphicsPath curve, fill(G::FillModeWinding);
        curve.AddLines(pts.data(), static_cast<INT>(pts.size()));
        std::vector<G::PointF> fp;
        fp.emplace_back(plotX, midY);
        fp.insert(fp.end(), pts.begin(), pts.end());
        fp.emplace_back(pts.back().X, midY);
        fill.AddPolygon(fp.data(), static_cast<INT>(fp.size()));

        c.setGradient(CYAN.withAlpha(0.20f), plotX + plotW * 0.5f, plotY, CYAN.withAlpha(0.01f), plotX + plotW * 0.5f, midY);
        c.fillPath(fill);
        c.setColour(CYAN.withAlpha(0.35f));
        c.strokePath(curve, 4.0f);
        c.setColour(CYAN);
        c.strokePath(curve, 1.8f);
        c.g().Restore(st);
    }

    void paintMeterRack(Canvas& c, RectF area, Layer layer) {
        G::GraphicsState st = c.g().Save();
        c.g().TranslateTransform(area.x, area.y);
        const RectF bounds{ 0, 0, area.w, area.h };
        const bool stat = layer == STATIC, dyn = layer == METERS;
        if (stat) {
            c.setColour(C(0xff12161f));
            c.fillRounded(bounds, 6.0f);
            c.setColour(C(0xff222937));
            c.drawRounded(bounds, 6.0f, 1.0f);
        }

        const float colW = rackColumnWidth(bounds.w);
        const float meterTopY = bounds.y + 26.0f;
        const float meterH = bounds.h - 56.0f;

        if (stat) {
            // dB scale on the left. As in the JUCE editor, only the first label is drawn in the
            // light colour: the grid line's colour carries over to the labels after it.
            c.setColour(C(0xff8a96a7));
            for (float db : { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f }) {
                const float y = meterTopY + (-db / 12.0f) * meterH;
                c.text(std::to_string(static_cast<int>(db)) + "dB", static_cast<int>(bounds.x + 2), static_cast<int>(y - 6),
                       static_cast<int>(SCALE_W), 12, CENTRED_RIGHT, 9.5f, true);
                c.setColour(C(0xff1f2532));
                c.hLine(static_cast<int>(y), bounds.x + SCALE_W + 4, bounds.right() - 6);
            }
        }

        for (size_t b = 0; b < Bands::COUNT; ++b) {
            const float bx = bounds.x + rackColumnX(b, bounds.w);
            const float barW = 34.0f, barX = bx + 10.0f, meterColW = barW + 20.0f;

            if (stat) {
                c.setColour(C(0xffd0d7e2));
                c.text(std::string(Bands::NAMES[b]), static_cast<int>(bx), static_cast<int>(bounds.y + 5), static_cast<int>(colW), 16, CENTRED, 11.0f, true);
                const RectF trough{ barX, meterTopY, barW, meterH };
                c.setColour(C(0xff0a0c10));
                c.fillRounded(trough, 3.0f);
                c.setColour(C(0xff1c222e));
                c.drawRounded(trough, 3.0f, 1.0f);
            }

            constexpr int SEGS = 12;
            const float gr = currentGr[b];
            const float normGr = std::clamp(-gr / 12.0f, 0.0f, 1.0f);
            const int active = dyn ? static_cast<int>(std::round(normGr * static_cast<float>(SEGS))) : 0;
            const float segH = (meterH - static_cast<float>(SEGS - 1) * 2.0f) / static_cast<float>(SEGS);
            for (int s = 0; s < SEGS; ++s) {
                // unlit segments belong to the static layer, lit ones are drawn over them
                const bool lit = s < active;
                if (stat) c.setColour(C(0xff141820));
                else if (lit) c.setColour(s < 3 ? CYAN : s < 6 ? AMBER : RED);
                else continue;
                const float sy = meterTopY + static_cast<float>(s) * (segH + 2.0f);
                c.fillRounded(RectF{ barX + 2.0f, sy, barW - 4.0f, segH }, 1.5f);
            }
            if (!dyn) continue;

            const float peak = peakGr[b];
            if (peak < -0.2f) {
                const float peakY = meterTopY + std::clamp(-peak / 12.0f, 0.0f, 1.0f) * (meterH - 2.0f);
                c.setColour(C(0xffffea00));
                c.fillRect(barX + 1.0f, peakY, barW - 2.0f, 2.0f);
            }

            c.setColour(gr < -0.1f ? CYAN : C(0xff7c889a));
            c.text(gr < -0.05f ? str1(gr) + "dB" : "0.0dB", static_cast<int>(bx), static_cast<int>(meterTopY + meterH + 4),
                   static_cast<int>(meterColW), 14, CENTRED, 10.5f, true);

            const float off = contour[b];
            c.setColour(off < 0.0f ? AMBER : off > 0.0f ? CYAN : C(0xff556070));
            c.text(off > 0.0f ? "+" + str1(off) : str1(off), static_cast<int>(bx), static_cast<int>(meterTopY + meterH + 18),
                   static_cast<int>(meterColW), 12, CENTRED, 9.5f, true);
        }
        c.g().Restore(st);
    }

    void paintContent(Canvas& c, Layer layer) {
        const bool stat = layer == STATIC, dyn = layer == METERS;
        auto drawCard = [&c](RectF b, const char* title) {
            c.setGradient(C(0xff141822), b.x, b.y, C(0xff0f121a), b.x, b.bottom());
            c.fillRounded(b, 6.0f);
            c.setColour(C(0xff242c3b));
            c.drawRounded(b, 6.0f, 1.0f);
            c.setColour(C(0xff8893a4));
            c.text(title, static_cast<int>(b.x) + 12, static_cast<int>(b.y) + 8, static_cast<int>(b.w) - 24, 16, CENTRED_LEFT, 10.5f, true);
        };

        if (stat) {
            c.fillAll(C(0xff0a0c10), W, H);

            // Header
            c.setGradient(C(0xff161a24), 0.0f, 0.0f, C(0xff0e1118), 0.0f, 54.0f);
            c.fillRect(0, 0, 840, 54);
            c.setColour(C(0xff222938));
            c.hLine(54, 0.0f, 840.0f);

            c.setColour(CYAN);
            c.text("AUTOLEVEL DJ", 22, 10, 160, 24, CENTRED_LEFT, 20.0f, true);
            const RectF badge{ 186.0f, 13.0f, 38.0f, 16.0f };
            c.setColour(CYAN.withAlpha(0.18f));
            c.fillRounded(badge, 3.0f);
            c.setColour(CYAN);
            c.drawRounded(badge, 3.0f, 1.0f);
            c.text("PRO", badge, CENTRED, 9.5f, true);
            c.setColour(C(0xff757f90));
            c.text("DYNAMIC BROADCAST & CLUB LEVELING SYSTEM", 22, 32, 340, 16, CENTRED_LEFT, 10.5f, false);

            drawCard(R(20, 64, 240, 162), "PERCEIVED LOUDNESS (EBU R128)");
            const RectF statusRect{ 36.0f, 188.0f, 208.0f, 24.0f };
            c.setColour(C(0xff18222f));
            c.fillRounded(statusRect, 4.0f);
            c.setColour(GREEN);
            c.drawRounded(statusRect, 4.0f, 1.0f);
            c.text("EBU R128 DUAL-GATED CALIBRATED", statusRect, CENTRED, 9.5f, true);

            drawCard(R(270, 64, 240, 162), "AGC GAIN CORRECTION");
            drawCard(R(520, 64, 300, 162), "TONAL TARGET & SPECTRUM CURVE");
            drawCard(R(20, 236, 800, 198), "TONE SHAPER");
            drawCard(R(20, 444, 800, 244), "MASTER PROCESSOR CONTROLS");
        }

        // Card 1: Perceived Loudness
        if (dyn) {
            const float integ = latest.loudness.integratedLUFS;
            if (integ > -70.0f) {
                c.setColour(GREEN);
                c.text(str1(integ) + " LUFS", 20, 64 + 28, 240, 36, CENTRED, 32.0f, true);
            } else {
                c.setColour(C(0xff4a5360));
                c.text("---.- LUFS", 20, 64 + 28, 240, 36, CENTRED, 32.0f, true);
            }
            const float mom = latest.loudness.momentaryLUFS, stl = latest.loudness.shortTermLUFS;
            const std::string momStr = mom > -70.0f ? str1(mom) + " LUFS" : "--.-";
            const std::string stStr = stl > -70.0f ? str1(stl) + " LUFS" : "--.-";
            c.setColour(C(0xff9aa3b0));
            c.text("M (400ms): " + momStr + "  |  S (3s): " + stStr, 20 + 8, 64 + 68, 240 - 16, 16, CENTRED, 11.0f, false);

            const float targetLufs = value(Param::TARGET_LUFS);
            const float delta = integ > -70.0f ? integ - targetLufs : 0.0f;
            c.setColour(std::abs(delta) < 1.0f ? GREEN : AMBER);
            c.text("Target Delta: " + std::string(delta >= 0.0f ? "+" : "") + str1(delta) + " LU", 20 + 12, 64 + 90, 240 - 24, 16, CENTRED, 11.5f, true);
        }

        // Card 2: AGC Gain Correction
        const float mbX = 290.0f, mbY = 134.0f, mbW = 200.0f, mbH = 10.0f;
        const float centreX = mbX + mbW * 0.5f;
        if (stat) {
            c.setColour(C(0xff0a0c10));
            c.fillRounded(mbX, mbY, mbW, mbH, 3.0f);
        }
        if (dyn) {
            const float gain = latest.appliedGainDb;
            if (gain >= 0.0f) {
                c.setColour(CYAN);
                c.text("+" + str1(gain) + " dB", 270, 64 + 28, 240, 36, CENTRED, 34.0f, true);
            } else {
                c.setColour(AMBER);
                c.text(str1(gain) + " dB", 270, 64 + 28, 240, 36, CENTRED, 34.0f, true);
            }
            const float normGain = std::clamp(gain / 12.0f, -1.0f, 1.0f);
            if (normGain > 0.0f) {
                c.setColour(CYAN);
                c.fillRounded(centreX, mbY, normGain * (mbW * 0.5f), mbH, 2.0f);
            } else if (normGain < 0.0f) {
                c.setColour(AMBER);
                const float fw = -normGain * (mbW * 0.5f);
                c.fillRounded(centreX - fw, mbY, fw, mbH, 2.0f);
            }
            c.setColour(C(0xff2d3646));
            c.vLine(static_cast<int>(centreX), mbY - 2.0f, mbY + mbH + 2.0f);

            c.setColour(C(0xff9aa3b0));
            const std::string hl = latest.activeHalfLifeSeconds > 0.0f
                                       ? "T1/2: " + std::to_string(static_cast<int>(latest.activeHalfLifeSeconds)) + "s"
                                       : "Track Hold";
            c.text("Target: " + std::string(latest.targetGainDb >= 0.0f ? "+" : "") + str1(latest.targetGainDb) + " dB (" + hl + ")",
                   270, 64 + 88, 240, 18, CENTRED, 11.0f, false);
        }

        // OUT / GR meter
        const float bx = 726.0f, by = 458.0f, bw = 84.0f, bh = 222.0f;
        const float barTopY = by + 46.0f, barH = 126.0f;
        const float grBarX = bx + 7.0f, grBarW = 11.0f;
        const float labelY = barTopY + barH + 5.0f;
        const float lBarX = bx + 24.0f, rBarX = bx + 36.0f, pw = 9.0f;
        auto dbToY = [=](float db) { return barTopY + (1.0f - std::clamp((db + 36.0f) / 36.0f, 0.0f, 1.0f)) * barH; };
        const float heldGr = maxHeldLimiterGrDb;
        const float curGr = latest.limiterGainReductionDb;
        const float ceilingDb = value(Param::CEILING_DB);

        if (stat) {
            c.setGradient(C(0xff121620), bx, by, C(0xff0b0e14), bx, by + bh);
            c.fillRounded(RectF{ bx, by, bw, bh }, 5.0f);
            c.setColour(C(0xff222a38));
            c.drawRounded(RectF{ bx, by, bw, bh }, 5.0f, 1.0f);
            c.setColour(C(0xff8a96a7));
            c.text("OUT / GR", static_cast<int>(bx), static_cast<int>(by + 5.0f), static_cast<int>(bw), 14, CENTRED, 10.0f, true);

            c.setColour(C(0xff090b10));
            c.fillRounded(RectF{ grBarX, barTopY, grBarW, barH }, 2.0f);
            c.setColour(C(0xff1a212d));
            c.drawRounded(RectF{ grBarX, barTopY, grBarW, barH }, 2.0f, 1.0f);

            c.setColour(C(0xff090b10));
            c.fillRounded(RectF{ lBarX, barTopY, pw, barH }, 2.0f);
            c.fillRounded(RectF{ rBarX, barTopY, pw, barH }, 2.0f);
            c.setColour(C(0xff1a212d));
            c.drawRounded(RectF{ lBarX, barTopY, pw, barH }, 2.0f, 1.0f);
            c.drawRounded(RectF{ rBarX, barTopY, pw, barH }, 2.0f, 1.0f);

            c.setColour(C(0xff758394));
            c.text("L", static_cast<int>(lBarX - 1.0f), static_cast<int>(labelY), static_cast<int>(pw + 2.0f), 12, CENTRED, 8.5f, true);
            c.text("R", static_cast<int>(rBarX - 1.0f), static_cast<int>(labelY), static_cast<int>(pw + 2.0f), 12, CENTRED, 8.5f, true);

            for (float t : { -3.0f, -18.0f }) {
                c.setColour(C(0xff1d2533));
                c.hLine(static_cast<int>(std::round(dbToY(t))), rBarX + pw + 2.0f, rBarX + pw + 6.0f);
            }
            for (float t : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f }) {
                const float ty = dbToY(t);
                c.setColour(C(0xff252e3e));
                c.hLine(static_cast<int>(std::round(ty)), rBarX + pw + 2.0f, rBarX + pw + 7.0f);
                const bool isZero = std::abs(t) < 0.1f, isBottom = std::abs(t + 36.0f) < 0.1f;
                c.setColour(isZero ? RED : C(0xff657283));
                const float textY = isZero ? ty - 2.0f : isBottom ? ty - 9.0f : ty - 5.0f;
                c.text(isZero ? "0" : std::to_string(static_cast<int>(t)), static_cast<int>(bx + 48.0f), static_cast<int>(textY), 30, 11, CENTRED_LEFT, 8.5f, true);
            }
        }
        if (!dyn) return;

        const RectF grBadge{ bx + 6.0f, by + 20.0f, bw - 12.0f, 17.0f };
        if (heldGr < -0.05f) {
            c.setColour(C(0xff2a141b));
            c.fillRounded(grBadge, 3.0f);
            c.setColour(RED);
            c.drawRounded(grBadge, 3.0f, 1.0f);
            c.text(str1(heldGr) + " dB", grBadge, CENTRED, 9.5f, true);
        } else {
            c.setColour(C(0xff10141d));
            c.fillRounded(grBadge, 3.0f);
            c.setColour(C(0xff1e2634));
            c.drawRounded(grBadge, 3.0f, 1.0f);
            c.setColour(C(0xff606c7d));
            c.text("0.0 dB GR", grBadge, CENTRED, 9.0f, true);
        }

        const float normLimGr = std::clamp(-curGr / 36.0f, 0.0f, 1.0f);
        if (normLimGr > 0.005f) {
            const float fh = normLimGr * (barH - 2.0f);
            c.setGradient(C(0xffffea00), grBarX, barTopY, RED, grBarX, barTopY + fh);
            c.fillRounded(RectF{ grBarX + 1.0f, barTopY + 1.0f, grBarW - 2.0f, fh }, 1.5f);
        }
        const float normHeld = std::clamp(-heldGr / 36.0f, 0.0f, 1.0f);
        if (normHeld > 0.008f) {
            c.setColour(RED);
            c.fillRect(grBarX + 1.0f, barTopY + 1.0f + normHeld * (barH - 3.0f), grBarW - 2.0f, 2.0f);
        }
        c.setColour(curGr < -0.05f ? RED : C(0xff556272));
        c.text("GR", static_cast<int>(grBarX - 1.0f), static_cast<int>(labelY), static_cast<int>(grBarW + 2.0f), 12, CENTRED, 8.5f, true);

        auto peakFill = [&](float x, float peakDb) {
            const float norm = std::clamp((peakDb + 36.0f) / 36.0f, 0.0f, 1.0f);
            if (norm > 0.01f) {
                const float fh = norm * (barH - 2.0f);
                const float fy = barTopY + barH - 1.0f - fh;
                const Col top = peakDb > -3.0f ? RED : peakDb > -12.0f ? AMBER : CYAN;
                c.setGradient(top, x, fy, CYAN.withAlpha(0.6f), x, barTopY + barH);
                c.fillRounded(RectF{ x + 1.0f, fy, pw - 2.0f, fh }, 1.5f);
            }
        };
        peakFill(lBarX, latest.outputPeakDbL);
        peakFill(rBarX, latest.outputPeakDbR);

        c.setColour(RED);
        c.fillRect(lBarX - 1.0f, dbToY(ceilingDb) - 0.5f, (rBarX + pw - lBarX) + 2.0f, 1.5f);

        const float maxPeak = std::max(latest.outputPeakDbL, latest.outputPeakDbR);
        const std::string peakStr = maxPeak > -50.0f ? str1(maxPeak) + " dBFS" : "---.- dBFS";
        const RectF peakBadge{ bx + 6.0f, by + 195.0f, bw - 12.0f, 20.0f };
        c.setColour(C(0xff090c12));
        c.fillRounded(peakBadge, 3.5f);
        const bool atCeiling = maxPeak >= ceilingDb - 0.1f;
        c.setColour(atCeiling ? RED : C(0xff18202d));
        c.drawRounded(peakBadge, 3.5f, 1.0f);
        c.setColour(atCeiling ? RED : CYAN);
        c.text(peakStr, peakBadge, CENTRED, 9.5f, true);
    }

    struct Surface {
        HDC dc = nullptr;
        HBITMAP bmp = nullptr;
        HGDIOBJ old = nullptr;
        void create(HDC like, int w, int h) {
            dc = CreateCompatibleDC(like);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            bmp = CreateDIBSection(like, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            old = SelectObject(dc, bmp);
        }
        void destroy() {
            if (dc != nullptr) {
                SelectObject(dc, old);
                DeleteObject(bmp);
                DeleteDC(dc);
            }
            dc = nullptr;
            bmp = nullptr;
        }
    };
    Surface staticLayer, controlsLayer, frame;
    std::vector<std::vector<float>> partSigs;

    // Controls, in paint order: tone curve, knobs, buttons, faders.
    size_t controlCount() const { return 1 + knobs.size() + buttons.size() + faders.size(); }

    RectF controlBounds(size_t i) const {
        if (i == 0) return R(530, 138, 280, 80);
        i -= 1;
        if (i < knobs.size()) return knobs[i].bounds;
        i -= knobs.size();
        if (i < buttons.size()) return buttons[i].bounds;
        i -= buttons.size();
        return faders[i].bounds;
    }

    void paintControl(Canvas& c, size_t i) {
        if (i == 0) { paintToneCurve(c, R(530, 138, 280, 80)); return; }
        i -= 1;
        if (i < knobs.size()) { paintKnob(c, knobs[i]); return; }
        i -= knobs.size();
        if (i < buttons.size()) { paintButton(c, buttons[i], static_cast<int>(i)); return; }
        i -= buttons.size();
        paintFader(c, faders[i]);
    }

    std::vector<std::vector<float>> controlSignatures() const {
        std::vector<std::vector<float>> out;
        std::vector<float> curve{ value(Param::TONE_SLOPE) };
        for (int b = 0; b < 6; ++b) curve.push_back(plugin.hostValue(idx(Param::CUSTOM_BAND_0) + b));
        out.push_back(curve);
        for (const auto& k : knobs)
            out.push_back({ plugin.hostValue(k.param), editParam == k.param ? 1.0f : 0.0f });
        for (size_t i = 0; i < buttons.size(); ++i) {
            const auto& b = buttons[i];
            const int ii = static_cast<int>(i);
            out.push_back({ b.toggled && b.toggled() ? 1.0f : 0.0f, b.enabled && !b.enabled() ? 1.0f : 0.0f,
                            hoverButton == ii ? 1.0f : 0.0f, pressedButton == ii ? 1.0f : 0.0f,
                            pressedButton == ii && pressedInside ? 1.0f : 0.0f });
        }
        for (const auto& f : faders) {
            const int p = faderParam(f.band);
            out.push_back({ static_cast<float>(p), plugin.hostValue(p), editParam == p ? 1.0f : 0.0f });
        }
        return out;
    }
    bool staticValid = false;
    std::vector<float> lastControlsSig, lastFrameSig;

    void render(HDC target) {
        const int w = pixelW(), h = pixelH();
        if (frame.dc == nullptr || memW != w || memH != h) {
            freeBackBuffer();
            HDC screen = GetDC(hwnd);
            staticLayer.create(screen, w, h);
            controlsLayer.create(screen, w, h);
            frame.create(screen, w, h);
            ReleaseDC(hwnd, screen);
            memW = w;
            memH = h;
        }
        if (!staticValid) {
            G::Graphics gr(staticLayer.dc);
            paintLayer(gr, STATIC);
            staticValid = true;
            lastControlsSig.clear();
        }
        const std::vector<float> sig = controlsSignature();
        if (sig != lastControlsSig) {
            std::vector<std::vector<float>> parts = controlSignatures();
            if (lastControlsSig.empty() || partSigs.size() != parts.size()) {
                BitBlt(controlsLayer.dc, 0, 0, w, h, staticLayer.dc, 0, 0, SRCCOPY);
                G::Graphics gr(controlsLayer.dc);
                paintLayer(gr, CONTROLS);
            } else {
                // Only the controls that changed: restore their patch of the static layer, then
                // repaint them clipped to it. Controls never overlap, so this equals a full redraw.
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (parts[i] == partSigs[i]) continue;
                    const RectF r = controlBounds(i);
                    const int px = std::max(0, static_cast<int>(std::floor(r.x * scale)) - 1);
                    const int py = std::max(0, static_cast<int>(std::floor(r.y * scale)) - 1);
                    const int pw = static_cast<int>(std::ceil(r.right() * scale)) + 1 - px;
                    const int ph = static_cast<int>(std::ceil(r.bottom() * scale)) + 1 - py;
                    BitBlt(controlsLayer.dc, px, py, pw, ph, staticLayer.dc, px, py, SRCCOPY);
                    G::Graphics gr(controlsLayer.dc);
                    gr.SetClip(G::Rect(px, py, pw, ph));
                    setupGraphics(gr);
                    Canvas c(gr, *fonts);
                    paintControl(c, i);
                }
            }
            partSigs = std::move(parts);
            lastControlsSig = sig;
        }
        BitBlt(frame.dc, 0, 0, w, h, controlsLayer.dc, 0, 0, SRCCOPY);
        {
            G::Graphics gr(frame.dc);
            paintLayer(gr, METERS);
        }
        BitBlt(target, 0, 0, w, h, frame.dc, 0, 0, SRCCOPY);
    }

    void freeBackBuffer() {
        staticLayer.destroy();
        controlsLayer.destroy();
        frame.destroy();
        staticValid = false;
        lastControlsSig.clear();
        partSigs.clear();
        memW = memH = 0;
    }

    // --------------------------------------------------------------------------------------
    // Mouse

    void toContent(LPARAM lp, float& x, float& y) const {
        x = static_cast<float>(GET_X_LPARAM(lp)) / scale;
        y = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
    }

    int buttonAt(float x, float y) const {
        for (int i = static_cast<int>(buttons.size()) - 1; i >= 0; --i) {
            const auto& b = buttons[static_cast<size_t>(i)];
            if (b.bounds.contains(x, y) && (!b.enabled || b.enabled())) return i;
        }
        return -1;
    }

    bool inResizer(LPARAM lp) const {
        return canResize && GET_X_LPARAM(lp) >= pixelW() - RESIZER && GET_Y_LPARAM(lp) >= pixelH() - RESIZER;
    }

    void onMouseDown(LPARAM lp, bool doubleClick) {
        endEdit(true);
        SetFocus(hwnd);
        float x, y;
        toContent(lp, x, y);
        dragMoved = false;
        dragStartX = x;
        dragStartY = y;

        if (inResizer(lp)) {
            drag = DragKind::Resizer;
            GetCursorPos(&resizeStart);
            resizeStartW = pixelW();
            SetCapture(hwnd);
            return;
        }

        // Faders sit above the meter rack and the knobs are separate; buttons are on top.
        const int bi = buttonAt(x, y);
        if (bi >= 0) {
            drag = DragKind::Button;
            pressedButton = bi;
            pressedInside = true;
            SetCapture(hwnd);
            redraw();
            return;
        }
        for (int i = static_cast<int>(faders.size()) - 1; i >= 0; --i) {
            const auto& f = faders[static_cast<size_t>(i)];
            if (!f.bounds.contains(x, y)) continue;
            const int param = faderParam(f.band);
            if (f.valueBox().contains(x, y)) {
                drag = DragKind::ValueBox;
                dragParam = param;
                dragIndex = -1 - i;
                SetCapture(hwnd);
                return;
            }
            if (doubleClick) { // setDoubleClickReturnValue(true, 0.0)
                plugin.changeFromEditor(param, 0.0f);
                drag = DragKind::None;
                redraw();
                return;
            }
            drag = DragKind::Fader;
            dragIndex = i;
            dragParam = param;
            plugin.beginEdit(param);
            SetCapture(hwnd);
            faderTo(f, y);
            return;
        }
        for (int i = static_cast<int>(knobs.size()) - 1; i >= 0; --i) {
            const auto& k = knobs[static_cast<size_t>(i)];
            if (!k.bounds.contains(x, y)) continue;
            if (k.valueBox().contains(x, y)) {
                drag = DragKind::ValueBox;
                dragParam = k.param;
                dragIndex = i;
                SetCapture(hwnd);
                return;
            }
            drag = DragKind::Knob;
            dragIndex = i;
            dragParam = k.param;
            dragStartProp = plugin.normalised(k.param);
            plugin.beginEdit(k.param);
            SetCapture(hwnd);
            return;
        }

        // Click on the OUT / GR meter resets the held limiter gain reduction
        if (R(726, 458, 84, 222).contains(x, y)) {
            maxHeldLimiterGrDb = latest.limiterGainReductionDb;
            redraw();
        }
    }

    void faderTo(const Fader& f, float y) {
        const float p = std::clamp(1.0f - (y - f.regionStart()) / f.regionSize(), 0.0f, 1.0f);
        plugin.setFromEditor(dragParam, fromNormalised(dragParam, p));
        redraw();
    }

    void onMouseMove(LPARAM lp) {
        float x, y;
        toContent(lp, x, y);
        if (!tracking) {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            tracking = true;
        }
        if (std::abs(x - dragStartX) > 2.0f || std::abs(y - dragStartY) > 2.0f) dragMoved = true;

        switch (drag) {
            case DragKind::Knob: {
                // RotaryHorizontalVerticalDrag: right or up raises, 250 px for the full travel
                const float diff = (x - dragStartX) + (dragStartY - y);
                const float p = std::clamp(dragStartProp + diff * (1.0f / 250.0f), 0.0f, 1.0f);
                plugin.setFromEditor(dragParam, fromNormalised(dragParam, p));
                redraw();
                return;
            }
            case DragKind::Fader: faderTo(faders[static_cast<size_t>(dragIndex)], y); return;
            case DragKind::Button: {
                const bool inside = buttons[static_cast<size_t>(pressedButton)].bounds.contains(x, y);
                if (inside != pressedInside) { pressedInside = inside; redraw(); }
                return;
            }
            case DragKind::Resizer: {
                POINT pt;
                GetCursorPos(&pt);
                const int wantW = std::clamp(resizeStartW + static_cast<int>(pt.x - resizeStart.x), MIN_W, MAX_W);
                setScale(static_cast<float>(wantW) / static_cast<float>(W));
                return;
            }
            default: break;
        }
        const int hb = buttonAt(x, y);
        if (hb != hoverButton) { hoverButton = hb; redraw(); }
    }

    void onMouseUp(LPARAM lp) {
        float x, y;
        toContent(lp, x, y);
        const DragKind was = drag;
        drag = DragKind::None;
        ReleaseCapture();
        switch (was) {
            case DragKind::Knob:
            case DragKind::Fader: plugin.endEdit(dragParam); break;
            case DragKind::Button: {
                const int bi = pressedButton;
                pressedButton = -1;
                if (bi >= 0 && buttons[static_cast<size_t>(bi)].bounds.contains(x, y)) {
                    auto& b = buttons[static_cast<size_t>(bi)];
                    if (b.onClick) b.onClick();
                }
                break;
            }
            case DragKind::ValueBox: {
                // juce::Label edits on a single click that was not a drag
                const RectF box = dragIndex >= 0 ? knobs[static_cast<size_t>(dragIndex)].valueBox()
                                                 : faders[static_cast<size_t>(-1 - dragIndex)].valueBox();
                if (!dragMoved && box.contains(x, y)) beginEdit(dragParam, box);
                break;
            }
            default: break;
        }
        redraw();
    }

    void onWheel(WPARAM wp, LPARAM lp) {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        const float x = static_cast<float>(pt.x) / scale, y = static_cast<float>(pt.y) / scale;
        int param = -1;
        for (const auto& k : knobs) if (k.bounds.contains(x, y)) param = k.param;
        for (const auto& f : faders) if (f.bounds.contains(x, y)) param = faderParam(f.band);
        if (param < 0 || drag != DragKind::None) return;
        // juce::Slider::mouseWheelMove: 0.15 x the wheel delta (120 per notch -> 0.234) of the travel
        const float amount = std::clamp(0.5f * static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)), -1000.0f, 1000.0f) / 256.0f;
        const float cur = plugin.normalised(param);
        const float target = fromNormalised(param, std::clamp(cur + amount * 0.15f, 0.0f, 1.0f));
        if (target == plugin.hostValue(param)) return;
        plugin.changeFromEditor(param, target);
        redraw();
    }

    void setCursorFor(LPARAM screenPos) {
        POINT pt{ GET_X_LPARAM(screenPos), GET_Y_LPARAM(screenPos) };
        ScreenToClient(hwnd, &pt);
        const LPARAM lp = MAKELPARAM(pt.x, pt.y);
        if (inResizer(lp) || drag == DragKind::Resizer) { SetCursor(LoadCursor(nullptr, IDC_SIZENWSE)); return; }
        float x, y;
        toContent(lp, x, y);
        SetCursor(LoadCursor(nullptr, R(732, 478, 72, 18).contains(x, y) ? IDC_HAND : IDC_ARROW));
    }

    void redraw() { if (hwnd != nullptr) InvalidateRect(hwnd, nullptr, FALSE); }

    void setScale(float s) {
        s = std::clamp(s, static_cast<float>(MIN_W) / W, static_cast<float>(MAX_W) / W);
        const int w = static_cast<int>(std::lround(W * s)), h = static_cast<int>(std::lround(H * s));
        if (w == pixelW() && h == pixelH()) return;
        if (plugin.hostCall(vst2::audioMasterSizeWindow, w, h) == 0) return;
        endEdit(true);
        scale = s;
        plugin.editorScale = s;
        SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        redraw();
    }

    // --------------------------------------------------------------------------------------
    // Typing a value (the slider's value box, a juce::Label editable on single click)

    static LRESULT CALLBACK editWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetPropW(h, L"aldj.impl"));
        if (self == nullptr) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
            case WM_GETDLGCODE: return DLGC_WANTALLKEYS | CallWindowProcW(self->editProc, h, msg, wp, lp);
            case WM_KEYDOWN:
                if (wp == VK_RETURN || wp == VK_TAB) { PostMessageW(self->hwnd, WM_ALDJ_END_EDIT, 1, 0); return 0; }
                if (wp == VK_ESCAPE) { PostMessageW(self->hwnd, WM_ALDJ_END_EDIT, 0, 0); return 0; }
                break;
            case WM_CHAR:
                if (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB) return 0;
                break;
            case WM_KILLFOCUS: PostMessageW(self->hwnd, WM_ALDJ_END_EDIT, 1, 0); break;
            default: break;
        }
        return CallWindowProcW(self->editProc, h, msg, wp, lp);
    }

    void beginEdit(int param, RectF box) {
        endEdit(true);
        editParam = param;
        const int x = static_cast<int>(std::lround(box.x * scale)), y = static_cast<int>(std::lround(box.y * scale));
        const int w = static_cast<int>(std::lround(box.w * scale)), h = static_cast<int>(std::lround(box.h * scale));
        if (editFont != nullptr) DeleteObject(editFont);
        bool knobBox = false;
        for (const auto& k : knobs) if (k.param == param) knobBox = true;
        const float fontH = knobBox ? 15.0f : 11.5f;
        const int em = static_cast<int>(std::lround(fonts->emFor(fontH, !knobBox) * scale));
        editFont = CreateFontW(-em, 0, 0, 0, knobBox ? FW_NORMAL : FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, fonts->familyName().c_str());
        // A single-line edit centres nothing vertically: inset it so the text sits mid-box.
        const int pad = std::max(1, (h - static_cast<int>(std::lround(fontH * scale))) / 2);
        edit = CreateWindowExW(0, L"EDIT", widen(valueText(param, plugin.hostValue(param))).c_str(),
                               WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL,
                               x + 2, y + pad, w - 4, h - 2 * pad, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (edit == nullptr) { editParam = -1; return; }
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(editFont), FALSE);
        SetPropW(edit, L"aldj.impl", this);
        editProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&editWndProc)));
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        redraw();
    }

    void endEdit(bool apply) {
        if (edit == nullptr) return;
        HWND e = edit;
        const int param = editParam;
        edit = nullptr;
        editParam = -1;
        if (apply) {
            wchar_t buf[128] = {};
            GetWindowTextW(e, buf, 127);
            float v;
            if (parseText(param, narrow(buf), v)) plugin.changeFromEditor(param, v);
        }
        SetWindowLongPtrW(e, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProc));
        RemovePropW(e, L"aldj.impl");
        DestroyWindow(e);
        if (hwnd != nullptr && IsWindow(hwnd)) SetFocus(hwnd);
        redraw();
    }

    // --------------------------------------------------------------------------------------
    // Presets: box-format JSON files, interchangeable with autolevel-box's saved presets

    static std::wstring presetsFolder() {
        wchar_t docs[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs))) return {};
        std::wstring dir = std::wstring(docs) + L"\\AutoLevel";
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\Presets";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

    void showPresetsMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 100, L"Box-format presets (.json)");
        MENUITEMINFOW mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_STATE;
        mi.fState = MFS_DISABLED | MFS_DEFAULT;
        SetMenuItemInfoW(menu, 100, FALSE, &mi);
        AppendMenuW(menu, MF_STRING, 1, L"Load preset...");
        AppendMenuW(menu, MF_STRING, 2, L"Save preset as...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 3, L"Reset to Default");

        const RectF b = R(W - 432, 13, 96, 28);
        POINT pt{ static_cast<LONG>(std::lround(b.x * scale)), static_cast<LONG>(std::lround(b.bottom() * scale)) };
        ClientToScreen(hwnd, &pt);
        const int result = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);

        HWND owner = GetAncestor(hwnd, GA_ROOT);
        if (result == 3) {
            plugin.resetToDefaults();
        } else if (result == 1) {
            const std::wstring folder = presetsFolder();
            wchar_t file[MAX_PATH] = {};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = owner;
            ofn.lpstrFilter = L"AutoLevel presets (*.json)\0*.json\0All files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrInitialDir = folder.c_str();
            ofn.lpstrTitle = L"Load AutoLevel preset";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
            if (GetOpenFileNameW(&ofn)) {
                std::string json;
                if (readFile(file, json)) plugin.loadPresetJson(json, false, true);
            }
        } else if (result == 2) {
            const std::wstring folder = presetsFolder();
            wchar_t file[MAX_PATH] = L"My Preset.json";
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = owner;
            ofn.lpstrFilter = L"AutoLevel presets (*.json)\0*.json\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrInitialDir = folder.c_str();
            ofn.lpstrTitle = L"Save AutoLevel preset";
            ofn.lpstrDefExt = L"json";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
            if (GetSaveFileNameW(&ofn)) {
                std::wstring path = file;
                const size_t slash = path.find_last_of(L"\\/");
                const size_t dot = path.find_last_of(L'.');
                if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) path.erase(dot);
                path += L".json";
                if (!writeFile(path, plugin.presetJson()))
                    MessageBoxW(owner, (L"Could not write " + path).c_str(), L"Preset not saved", MB_OK | MB_ICONWARNING);
            }
        }
        redraw();
    }

    static bool readFile(const std::wstring& path, std::string& out) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (f == nullptr) return false;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        fclose(f);
        return true;
    }

    static bool writeFile(const std::wstring& path, const std::string& data) {
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (f == nullptr) return false;
        const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
        return fclose(f) == 0 && ok;
    }

    // --------------------------------------------------------------------------------------

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(h, msg, wp, lp);
        }
        if (self == nullptr) return DefWindowProcW(h, msg, wp, lp);
        switch (msg) {
            case WM_ERASEBKGND: return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(h, &ps);
                self->render(dc);
                EndPaint(h, &ps);
                return 0;
            }
            case WM_TIMER:
                if (wp == TIMER_ID) self->tick();
                return 0;
            case WM_LBUTTONDOWN: self->onMouseDown(lp, false); return 0;
            case WM_LBUTTONDBLCLK: self->onMouseDown(lp, true); return 0;
            case WM_MOUSEMOVE: self->onMouseMove(lp); return 0;
            case WM_LBUTTONUP: self->onMouseUp(lp); return 0;
            case WM_MOUSELEAVE:
                self->tracking = false;
                if (self->hoverButton != -1) { self->hoverButton = -1; self->redraw(); }
                return 0;
            case WM_MOUSEWHEEL: self->onWheel(wp, lp); return 0;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    DWORD pos = GetMessagePos();
                    self->setCursorFor(MAKELPARAM(GET_X_LPARAM(pos), GET_Y_LPARAM(pos)));
                    return TRUE;
                }
                break;
            case WM_CTLCOLOREDIT:
                SetTextColor(reinterpret_cast<HDC>(wp), RGB(255, 255, 255));
                SetBkColor(reinterpret_cast<HDC>(wp), RGB(0x12, 0x16, 0x1f));
                return reinterpret_cast<LRESULT>(self->editBrush);
            case WM_ALDJ_END_EDIT: self->endEdit(wp != 0); return 0;
            case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
            default: break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    static HINSTANCE moduleHandle() {
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&wndProc), &m);
        return m;
    }

    static const wchar_t* windowClass() {
        static std::wstring name;
        if (name.empty()) {
            wchar_t buf[64];
            swprintf(buf, 64, L"AutoLevelDJ_VST2_%p", static_cast<void*>(moduleHandle()));
            name = buf;
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.style = CS_DBLCLKS;
            wc.lpfnWndProc = &wndProc;
            wc.hInstance = moduleHandle();
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = name.c_str();
            RegisterClassExW(&wc);
        }
        return name.c_str();
    }

    bool open(HWND parent) {
        if (!acquireGdiplus()) return false;
        fonts = std::make_unique<Fonts>();
        editBrush = CreateSolidBrush(RGB(0x12, 0x16, 0x1f));
        scale = plugin.editorScale;
        canResize = plugin.hostCall(vst2::audioMasterCanDo, 0, 0, const_cast<char*>("sizeWindow")) == 1;
        if (!canResize) scale = 1.0f;
        hwnd = CreateWindowExW(0, windowClass(), L"AutoLevel DJ", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                               0, 0, pixelW(), pixelH(), parent, nullptr, moduleHandle(), this);
        if (hwnd == nullptr) { closeWindow(); return false; }
        tick();
        SetTimer(hwnd, TIMER_ID, 16, nullptr);
        return true;
    }

    void closeWindow() {
        if (edit != nullptr) endEdit(false);
        if (hwnd != nullptr) {
            KillTimer(hwnd, TIMER_ID);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
        freeBackBuffer();
        if (editFont != nullptr) DeleteObject(editFont);
        editFont = nullptr;
        if (editBrush != nullptr) DeleteObject(editBrush);
        editBrush = nullptr;
        if (fonts) {
            fonts.reset();
            releaseGdiplus();
        }
    }
};

Editor::Editor(Plugin& plugin) : m_plugin(plugin), m_impl(std::make_unique<Impl>(plugin)) {}

Editor::~Editor() { close(); }

bool Editor::open(void* parent) {
    close();
    return m_impl->open(static_cast<HWND>(parent));
}

void Editor::close() { m_impl->closeWindow(); }

vst2::ERect* Editor::rect() {
    const float s = m_impl->hwnd != nullptr ? m_impl->scale : m_plugin.editorScale;
    m_rect.top = 0;
    m_rect.left = 0;
    m_rect.bottom = static_cast<int16_t>(std::lround(H * s));
    m_rect.right = static_cast<int16_t>(std::lround(W * s));
    return &m_rect;
}

} // namespace aldj
