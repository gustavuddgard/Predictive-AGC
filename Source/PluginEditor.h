#pragma once

#include "PluginProcessor.h"

class Agc2AudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit Agc2AudioProcessorEditor(Agc2AudioProcessor&);
    ~Agc2AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class LookAndFeel;

    void timerCallback() override;
    void configureKnob(juce::Slider& slider, juce::Label& name, const juce::String& title);
    Agc2AudioProcessor& agc();

    std::unique_ptr<LookAndFeel> lookAndFeel;

    juce::Label title;
    juce::Label subtitle;
    juce::Label latencyReadout;
    juce::ComboBox presetBox;
    juce::Label levelerHeading;
    juce::Label sidechainHeading;

    juce::Label targetName;
    juce::Label boostName;
    juce::Label cutName;
    juce::Label floorName;
    juce::Label duckName;
    juce::Label speedName;
    juce::Label depthName;
    juce::Label thresholdName;
    juce::Label releaseName;
    juce::Label crossoverName;
    juce::Label senseName;
    juce::Slider target;
    juce::Slider maxBoost;
    juce::Slider maxCut;
    juce::Slider noiseFloor;
    juce::Slider duckAmount;
    juce::Slider speed;
    juce::Slider depth;
    juce::Slider scThreshold;
    juce::Slider scRelease;
    juce::Slider crossover;
    juce::Slider sense;

    juce::TextButton link { "Link" };
    juce::TextButton bypass { "Bypass" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> targetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> boostAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> floorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crossoverAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> senseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::Rectangle<int> meterArea;
    juce::Rectangle<int> lampArea;
    juce::Rectangle<int> lampLabelArea;
    float gainDb = 0.0f;
    float engageProbability = 0.0f;
    bool engaged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Agc2AudioProcessorEditor)
};
