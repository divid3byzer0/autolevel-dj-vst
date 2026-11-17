#pragma once
/*
 * AutoLevel DJ's parameters for the VST2 build: the same 30 settings, ranges, defaults, travel
 * (skew), snapping and display text as the JUCE build (src/PluginProcessor.cpp), without JUCE.
 * Index order is autolevel::AudioEngine::Param, which is also the VST2 parameter index.
 *
 * Three value domains, as in the JUCE build:
 *  - normalised 0..1: what the host automates (VST2 setParameter/getParameter);
 *  - host value: the knob's value in its own unit, snapped to its decimal step (Low Cut's
 *    lowest step, 19.5, shows as "Off");
 *  - engine value: what AudioEngine::setParam takes (Low Cut "Off" is 0).
 */
#include "AudioEngine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>

namespace aldj {

using Engine = autolevel::AudioEngine;
using Param = Engine::Param;
constexpr int kNumParams = Engine::PARAM_COUNT;

enum class Kind { Float, Choice, Bool };
enum class Fmt { OneDec, Percent, SignedDb, AttackMs, ReleaseMs, Detector, LowCut, Choice, Bool };

struct ParamDef {
    const char* name;      // as the JUCE build names it
    const char* label;     // unit shown by hosts next to the value
    Kind kind;
    Fmt fmt;
    float lo, hi;
    double step;           // decimal step the value snaps to
    float skewCentre;      // > 0: log-style travel with this value at the centre
    float def;             // default host value
    const char* choices[4];
    int numChoices;
};

constexpr float kHpfOffHostValue = 19.5f; // Low Cut host value shown as "Off" (engine 0)

inline const ParamDef& def(int i) {
    using MBC = autolevel::dsp::MBCParams;
    static const ParamDef defs[kNumParams] = {
        { "Target LUFS", "LUFS", Kind::Float, Fmt::OneDec, -20.0f, -6.0f, 0.5, 0, -14.0f, {}, 0 },
        { "Max Boost", "dB", Kind::Float, Fmt::OneDec, 0.0f, 18.0f, 0.5, 0, 12.0f, {}, 0 },
        { "Max Cut", "dB", Kind::Float, Fmt::OneDec, 0.0f, 18.0f, 0.5, 0, 12.0f, {}, 0 },
        { "Level Response", "", Kind::Float, Fmt::Percent, 0.0f, 1.0f, 0.01, 0, 0.85f, {}, 0 },
        { "Slew Speed", "", Kind::Choice, Fmt::Choice, 0, 2, 1, 0, 1, { "Slow", "Normal", "Fast" }, 3 },
        { "Compression", "", Kind::Float, Fmt::Percent, 0.0f, 1.0f, 0.01, 0, 0.5f, {}, 0 },
        { "Tone Slope", "dB/oct", Kind::Float, Fmt::OneDec, -3.0f, 0.0f, 0.1, 0, autolevel::dsp::Bands::DEFAULT_TONE_SLOPE, {}, 0 },
        { "MBC Attack", "ms", Kind::Float, Fmt::AttackMs, MBC::MIN_ATTACK_MS, MBC::MAX_ATTACK_MS, 0.1, MBC::DEFAULT_ATTACK_MS, MBC::DEFAULT_ATTACK_MS, {}, 0 },
        { "MBC Release", "ms", Kind::Float, Fmt::ReleaseMs, MBC::MIN_RELEASE_MS, MBC::MAX_RELEASE_MS, 1.0, MBC::DEFAULT_RELEASE_MS, MBC::DEFAULT_RELEASE_MS, {}, 0 },
        { "MBC Detector", "", Kind::Float, Fmt::Detector, 0.0f, 1.0f, 0.01, 0, 0.0f, {}, 0 },
        { "MBC Release Mode", "", Kind::Choice, Fmt::Choice, 0, 1, 1, 0, 0, { "Manual", "Auto" }, 2 },
        { "Air Lift", "", Kind::Choice, Fmt::Choice, 0, 3, 1, 0, 0, { "Off", "Low", "Med", "High" }, 4 },
        { "Post Gain", "", Kind::Float, Fmt::SignedDb, -24.0f, 24.0f, 0.1, 0, 0.0f, {}, 0 },
        { "Low Cut", "", Kind::Float, Fmt::LowCut, kHpfOffHostValue, 50.0f, 0.5, 0, 30.0f, {}, 0 },
        { "Limiter Ceiling", "dBFS", Kind::Float, Fmt::OneDec, -3.0f, 0.0f, 0.1, 0, -0.3f, {}, 0 },
        { "Freeze Breakdowns", "", Kind::Bool, Fmt::Bool, 0, 1, 1, 0, 1, {}, 0 },
        { "Breakdown Threshold", "LU", Kind::Float, Fmt::OneDec, 3.0f, 15.0f, 0.5, 0, 7.0f, {}, 0 },
        { "Bypass", "", Kind::Bool, Fmt::Bool, 0, 1, 1, 0, 0, {}, 0 },
        { "Contour Sub", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "Contour Bass", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "Contour Low-Mid", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "Contour High-Mid", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "Contour Presence", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "Contour Air", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "EQ Sub", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "EQ Bass", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "EQ Low-Mid", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "EQ High-Mid", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "EQ Presence", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
        { "EQ Air", "", Kind::Float, Fmt::SignedDb, -12.0f, 12.0f, 0.1, 0, 0.0f, {}, 0 },
    };
    return defs[i];
}

/** juce::NormalisableRange::setSkewForCentre, in float like JUCE. */
inline float skewFor(const ParamDef& d) {
    if (d.skewCentre <= 0.0f) return 1.0f;
    return std::log(0.5f) / std::log((d.skewCentre - d.lo) / (d.hi - d.lo));
}

/** Snap to the decimal step: the exact float the box parses for that decimal. */
inline float snap(const ParamDef& d, float v) {
    const double q = std::round(static_cast<double>(v) / d.step) * d.step;
    return std::clamp(static_cast<float>(q), d.lo, d.hi);
}

inline float toNormalised(int i, float hostValue) {
    const ParamDef& d = def(i);
    float p = std::clamp((hostValue - d.lo) / (d.hi - d.lo), 0.0f, 1.0f);
    const float skew = skewFor(d);
    if (skew != 1.0f) p = std::pow(p, skew);
    return p;
}

inline float fromNormalised(int i, float norm) {
    const ParamDef& d = def(i);
    float p = std::clamp(norm, 0.0f, 1.0f);
    const float skew = skewFor(d);
    if (skew != 1.0f && p > 0.0f) p = std::exp(std::log(p) / skew);
    return snap(d, d.lo + (d.hi - d.lo) * p);
}

inline float toEngineValue(int i, float hostValue) {
    if (static_cast<Param>(i) == Param::HPF_FREQ && hostValue < 20.0f) return 0.0f;
    return hostValue;
}

inline float toHostValue(int i, float engineValue) {
    if (static_cast<Param>(i) == Param::HPF_FREQ && engineValue <= 0.0f) return kHpfOffHostValue;
    return snap(def(i), engineValue);
}

inline std::string fmt1(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
    return b;
}

/** The value as the JUCE build's parameter text (what a host shows; knobs add a unit). */
inline std::string valueText(int i, float v) {
    const ParamDef& d = def(i);
    switch (d.fmt) {
        case Fmt::OneDec: return fmt1(v);
        case Fmt::Percent: return std::to_string(std::lround(v * 100.0f)) + "%";
        case Fmt::SignedDb: return (v > 0.04f ? "+" : "") + fmt1(v) + " dB";
        case Fmt::AttackMs: return fmt1(v) + " ms";
        case Fmt::ReleaseMs: return std::to_string(std::lround(v)) + " ms";
        case Fmt::Detector:
            if (v <= 0.0f) return "Peak";
            if (v >= 1.0f) return "RMS";
            return std::to_string(std::lround(v * 100.0f)) + "% RMS";
        case Fmt::LowCut: {
            if (v < 20.0f) return "Off";
            if (v == std::floor(v)) return std::to_string(static_cast<int>(v)) + " Hz";
            return fmt1(v) + " Hz";
        }
        case Fmt::Choice: {
            const int idx = std::clamp(static_cast<int>(std::lround(v)), 0, d.numChoices - 1);
            return d.choices[idx];
        }
        case Fmt::Bool: return v >= 0.5f ? "On" : "Off";
    }
    return {};
}

inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/**
 * Typed text -> host value (snapped), or false if it has no number. Units are optional. Percent
 * settings read the number as a percentage ("50", "50%" = 50%), and Detector and Low Cut also
 * accept "Peak"/"RMS" and "Off".
 */
inline bool parseText(int i, const std::string& text, float& out) {
    const ParamDef& d = def(i);
    std::string t = lower(text);
    t.erase(0, t.find_first_not_of(" \t"));
    if (d.fmt == Fmt::Detector) {
        if (t.rfind("peak", 0) == 0) { out = 0.0f; return true; }
        if (t.rfind("rms", 0) == 0) { out = 1.0f; return true; }
    }
    if (d.fmt == Fmt::LowCut && t.rfind("off", 0) == 0) { out = kHpfOffHostValue; return true; }
    if (d.kind == Kind::Choice) {
        for (int c = 0; c < d.numChoices; ++c)
            if (t.rfind(lower(d.choices[c]), 0) == 0) { out = static_cast<float>(c); return true; }
    }
    if (d.kind == Kind::Bool) {
        if (t.rfind("on", 0) == 0) { out = 1.0f; return true; }
        if (t.rfind("off", 0) == 0) { out = 0.0f; return true; }
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    float f = static_cast<float>(v);
    if (d.fmt == Fmt::Percent || d.fmt == Fmt::Detector) f /= 100.0f;
    if (d.fmt == Fmt::LowCut && f < 20.0f) { out = kHpfOffHostValue; return true; }
    if (d.kind != Kind::Float) f = static_cast<float>(std::lround(f));
    out = snap(d, f);
    return true;
}

} // namespace aldj
