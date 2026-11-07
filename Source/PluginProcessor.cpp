#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::AudioParameterFloatAttributes decibelText()
{
    return juce::AudioParameterFloatAttributes()
        .withLabel("dB")
        .withStringFromValueFunction([](float value, int)
        {
            return juce::String(value, 1);
        });
}
} // namespace

Agc2AudioProcessor::Agc2AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                               .withInput("Sidechain", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "AGC2", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout Agc2AudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "target", 1 }, "Target",
        juce::NormalisableRange<float> { -40.0f, 0.0f, 0.1f }, -18.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "maxBoost", 2 }, "Max boost",
        juce::NormalisableRange<float> { 0.0f, 6.0f, 0.1f }, 6.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "duck", 1 }, "Duck",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.4f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(juce::roundToInt(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "vad", 1 }, "Noise floor",
        juce::NormalisableRange<float> { -60.0f, -24.0f, 0.1f }, -42.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "maxCut", 1 }, "Max cut",
        juce::NormalisableRange<float> { 0.0f, 40.0f, 0.1f }, 24.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "speed", 1 }, "Speed",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(juce::roundToInt(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "duckDepth", 1 }, "Depth",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(juce::roundToInt(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "scThreshold", 1 }, "Threshold",
        juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, -24.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "scRelease", 1 }, "Release",
        juce::NormalisableRange<float> { 50.0f, 1000.0f, 1.0f, 0.5f }, 300.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(juce::roundToInt(value));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "crossover", 1 }, "Crossover",
        juce::NormalisableRange<float> { 100.0f, 1000.0f, 1.0f, 0.5f }, 300.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(juce::roundToInt(value));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "scVad", 1 }, "Sense",
        juce::NormalisableRange<float> { -60.0f, -20.0f, 0.1f }, -45.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "link", 1 }, "Link", true));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

void Agc2AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int mainChannels = std::max(1, getMainBusNumInputChannels());
    mAGC.prepare(sampleRate, mainChannels);
    mDucker.prepare(sampleRate, mainChannels);
    setLatencySamples(mAGC.getLatencySamples() + mDucker.getLatencySamples());
    scratch.setSize(std::max(1, getTotalNumInputChannels()), std::max(1, samplesPerBlock), false, false, true);
}

void Agc2AudioProcessor::releaseResources() {}

bool Agc2AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    if (input != juce::AudioChannelSet::mono() && input != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() < 2)
        return true;

    const auto sidechain = layouts.getChannelSet(true, 1);
    return sidechain.isDisabled()
        || sidechain == juce::AudioChannelSet::mono()
        || sidechain == juce::AudioChannelSet::stereo();
}

void Agc2AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    mAGC.setTargetDecibels(apvts.getRawParameterValue("target")->load());
    mAGC.setMaxBoostDecibels(apvts.getRawParameterValue("maxBoost")->load());
    mAGC.setMaxCutDecibels(apvts.getRawParameterValue("maxCut")->load());
    mAGC.setVadThresholdDecibels(apvts.getRawParameterValue("vad")->load());
    mAGC.setDuck(apvts.getRawParameterValue("duck")->load());
    mAGC.setSpeed(apvts.getRawParameterValue("speed")->load());
    mAGC.setLinked(apvts.getRawParameterValue("link")->load() >= 0.5f);
    mAGC.setBypassed(apvts.getRawParameterValue("bypass")->load() >= 0.5f);

    mDucker.setDuckDepth(apvts.getRawParameterValue("duckDepth")->load());
    mDucker.setThresholdDecibels(apvts.getRawParameterValue("scThreshold")->load());
    mDucker.setReleaseMilliseconds(apvts.getRawParameterValue("scRelease")->load());
    mDucker.setCrossoverFrequency(apvts.getRawParameterValue("crossover")->load());
    mDucker.setVadThresholdDecibels(apvts.getRawParameterValue("scVad")->load());

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    auto mainBus = getBusBuffer(buffer, true, 0);
    const float* const* sidechain = nullptr;
    int sidechainChannels = 0;
    juce::AudioBuffer<float> sideBus;
    if (getBusCount(true) > 1)
    {
        sideBus = getBusBuffer(buffer, true, 1);
        sidechainChannels = sideBus.getNumChannels();
        sidechain = sideBus.getArrayOfReadPointers();
    }

    if (mainBus.getNumChannels() > 0)
    {
        mAGC.process(mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(), mainBus.getNumSamples());
        mDucker.process(mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(),
                        sidechain, sidechainChannels, mainBus.getNumSamples());
    }

    displayedGainDb.store(mAGC.getGainDecibels(), std::memory_order_relaxed);
    displayedEngage.store(mAGC.getEngageProbability(), std::memory_order_relaxed);
    displayedEngaged.store(mAGC.isEngaged(), std::memory_order_relaxed);
}

void Agc2AudioProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    scratch.setSize(buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            scratch.setSample(channel, sample, static_cast<float>(buffer.getSample(channel, sample)));

    processBlock(scratch, midi);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(channel, sample, static_cast<double>(scratch.getSample(channel, sample)));
}

juce::AudioProcessorEditor* Agc2AudioProcessor::createEditor()
{
    return new Agc2AudioProcessorEditor(*this);
}

bool Agc2AudioProcessor::hasEditor() const { return true; }

const juce::String Agc2AudioProcessor::getName() const { return "AGC 2.0"; }

bool Agc2AudioProcessor::acceptsMidi() const { return false; }
bool Agc2AudioProcessor::producesMidi() const { return false; }
bool Agc2AudioProcessor::isMidiEffect() const { return false; }
double Agc2AudioProcessor::getTailLengthSeconds() const { return 0.0; }

int Agc2AudioProcessor::getNumPrograms() { return 1; }
int Agc2AudioProcessor::getCurrentProgram() { return 0; }
void Agc2AudioProcessor::setCurrentProgram(int) {}
const juce::String Agc2AudioProcessor::getProgramName(int) { return {}; }
void Agc2AudioProcessor::changeProgramName(int, const juce::String&) {}

void Agc2AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void Agc2AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Agc2AudioProcessor();
}
