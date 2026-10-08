#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/AutoLevelEngine.h"
#include <array>

class AutoLevelDJAudioProcessor : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
    AutoLevelDJAudioProcessor();
    ~AutoLevelDJAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return m_apvts; }
    autolevel::dsp::EngineVisualState getVisualState() const noexcept { return m_engine.getVisualState(); }
    void resetIntegration() noexcept { m_engine.reset(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Parameter ID constants
    static constexpr const char* ID_TARGET_LUFS = "target_lufs";
    static constexpr const char* ID_MAX_BOOST = "max_boost";
    static constexpr const char* ID_MAX_CUT = "max_cut";
    static constexpr const char* ID_LEVEL_RESPONSE = "level_response";
    static constexpr const char* ID_SLEW_SPEED = "slew_speed";
    static constexpr const char* ID_COMPRESSION_AMOUNT = "compression_amount";
    static constexpr const char* ID_TONE_SLOPE = "tone_slope";
    static constexpr const char* ID_TARGET_PROFILE = "target_profile";
    static constexpr const char* ID_MBC_ATTACK = "mbc_attack";
    static constexpr const char* ID_MBC_RELEASE = "mbc_release";
    static constexpr const char* ID_MBC_DETECTOR = "mbc_detector";
    static constexpr const char* ID_MBC_RELEASE_MODE = "mbc_release_mode";
    static constexpr const char* ID_EQ_POSITION = "eq_position";
    static constexpr const char* ID_LIMITER_LOOKAHEAD = "limiter_lookahead";
    static constexpr const char* ID_POST_MBC_GAIN = "post_mbc_gain";
    static constexpr const char* ID_HPF_FREQ = "hpf_freq";
    static constexpr const char* ID_CEILING_DB = "ceiling_db";
    static constexpr const char* ID_FREEZE_BREAKDOWNS = "freeze_breakdowns";
    static constexpr const char* ID_BYPASS = "bypass";

    /** Band EQ gain parameter IDs, in autolevel::dsp::Bands order (Sub ... Air). */
    static constexpr std::array<const char*, autolevel::dsp::Bands::COUNT> ID_EQ_BANDS = {
        "eq_sub", "eq_bass", "eq_lowmid", "eq_highmid", "eq_presence", "eq_air"
    };

private:
    autolevel::dsp::LimiterLookahead currentLookahead() const noexcept;
    /** Reports the latency for the current lookahead; runs on the message thread. */
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState m_apvts;
    autolevel::dsp::AutoLevelEngine m_engine;

    // Parameter pointers
    std::atomic<float>* m_targetLufsParam = nullptr;
    std::atomic<float>* m_maxBoostParam = nullptr;
    std::atomic<float>* m_maxCutParam = nullptr;
    std::atomic<float>* m_levelResponseParam = nullptr;
    std::atomic<float>* m_slewSpeedParam = nullptr;
    std::atomic<float>* m_compressionAmountParam = nullptr;
    std::atomic<float>* m_toneSlopeParam = nullptr;
    std::atomic<float>* m_targetProfileParam = nullptr;
    std::atomic<float>* m_mbcAttackParam = nullptr;
    std::atomic<float>* m_mbcReleaseParam = nullptr;
    std::atomic<float>* m_mbcDetectorParam = nullptr;
    std::atomic<float>* m_mbcReleaseModeParam = nullptr;
    std::atomic<float>* m_eqPositionParam = nullptr;
    std::atomic<float>* m_lookaheadParam = nullptr;
    std::atomic<double> m_sampleRate{48000.0};
    /** Lookahead the audio thread last saw; -1 = none yet. Latency is re-reported when it changes. */
    std::atomic<int> m_lastLookahead{-1};
    std::array<std::atomic<float>*, autolevel::dsp::Bands::COUNT> m_eqBandParams{};
    std::atomic<float>* m_postMbcGainParam = nullptr;
    std::atomic<float>* m_hpfFreqParam = nullptr;
    std::atomic<float>* m_ceilingDbParam = nullptr;
    std::atomic<float>* m_freezeBreakdownsParam = nullptr;
    std::atomic<float>* m_bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoLevelDJAudioProcessor)
};
