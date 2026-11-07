#pragma once

#include "PredictiveDucker.h"
#include "ProgramAdaptiveAGC.h"

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

class Agc2AudioProcessor : public juce::AudioProcessor
{
public:
    Agc2AudioProcessor();
    ~Agc2AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

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

    bool supportsDoublePrecisionProcessing() const override { return true; }

    float getDisplayedGainDb() const noexcept { return displayedGainDb.load(std::memory_order_relaxed); }
    float getDisplayedEngage() const noexcept { return displayedEngage.load(std::memory_order_relaxed); }
    bool getDisplayedEngaged() const noexcept { return displayedEngaged.load(std::memory_order_relaxed); }
    int getDisplayedClass() const noexcept { return displayedClass.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void applyProgram(int index);

    dsp::ProgramAdaptiveAGC mLevel;
    dsp::PredictiveDucker mDucker;
    juce::AudioBuffer<float> scratch;
    int currentProgram = 0;
    std::atomic<float> displayedGainDb { 0.0f };
    std::atomic<float> displayedEngage { 0.0f };
    std::atomic<bool> displayedEngaged { false };
    std::atomic<int> displayedClass { 0 };
    int publishedClass = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Agc2AudioProcessor)
};
