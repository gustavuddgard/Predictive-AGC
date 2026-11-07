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
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.01f }, 0.0f,
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
        juce::ParameterID { "lookahead", 1 }, "Lookahead",
        juce::NormalisableRange<float> { 1.5f, 40.0f, 0.1f, 0.5f }, 1.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, 1);
            })));

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

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "targetLoudness", 1 }, "Target Loudness",
        juce::NormalisableRange<float> { -30.0f, -6.0f, 0.1f }, -18.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "maxGainBoost", 1 }, "Max Gain Boost",
        juce::NormalisableRange<float> { 0.0f, 12.0f, 0.1f }, 6.0f, decibelText()));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "baseRelease", 1 }, "Base Release Time",
        juce::NormalisableRange<float> { 100.0f, 1500.0f, 1.0f, 0.5f }, 500.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(juce::roundToInt(value));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "peakLookahead", 1 }, "Look-Ahead Time",
        juce::NormalisableRange<float> { 0.5f, 3.0f, 0.01f }, 1.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, 2);
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "classifierState", 1 }, "Classifier State",
        juce::StringArray { "Silence", "Music", "Voice", "Telephone", "Stresser" }, 0));

    return layout;
}

void Agc2AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int mainChannels = std::max(1, getMainBusNumInputChannels());
    mLevel.setLookaheadMilliseconds(apvts.getRawParameterValue("peakLookahead")->load());
    mLevel.prepare(sampleRate, mainChannels);
    mDucker.prepare(sampleRate, mainChannels);
    setLatencySamples(mLevel.getLatencySamples() + mDucker.getLatencySamples());
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

    mLevel.setTargetDecibels(apvts.getRawParameterValue("targetLoudness")->load());
    mLevel.setMaxBoostDecibels(apvts.getRawParameterValue("maxGainBoost")->load());
    mLevel.setReleaseMilliseconds(apvts.getRawParameterValue("baseRelease")->load());
    mLevel.setLookaheadMilliseconds(apvts.getRawParameterValue("peakLookahead")->load());
    mLevel.setCrossoverFrequency(apvts.getRawParameterValue("crossover")->load());
    mLevel.setDuck(apvts.getRawParameterValue("duck")->load());
    mLevel.setVadThresholdDecibels(apvts.getRawParameterValue("vad")->load());
    mLevel.setBypassed(apvts.getRawParameterValue("bypass")->load() >= 0.5f);

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
        mLevel.process(mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(), mainBus.getNumSamples());
        mDucker.process(mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(),
                        sidechain, sidechainChannels, mainBus.getNumSamples());
    }

    const int latency = mLevel.getLatencySamples() + mDucker.getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples(latency);

    const int klass = static_cast<int>(mLevel.getClass());
    displayedClass.store(klass, std::memory_order_relaxed);
    if (klass != publishedClass)
    {
        publishedClass = klass;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("classifierState")))
        {
            if (choice->getIndex() != klass)
                choice->setValueNotifyingHost(choice->convertTo0to1(static_cast<float>(klass)));
        }
    }

    displayedGainDb.store(mLevel.getGainDecibels(), std::memory_order_relaxed);
    displayedEngage.store(mLevel.isLimiting() ? 1.0f : 0.0f, std::memory_order_relaxed);
    displayedEngaged.store(mLevel.isLimiting(), std::memory_order_relaxed);
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

namespace
{
struct Program
{
    const char* name;
    float target;
    float maxBoost;
    float maxCut;
    float duck;
    float vad;
    float speed;
    float lookahead;
    float targetLoudness;
    float maxGainBoost;
    float baseRelease;
    float peakLookahead;
    float depth;
    float threshold;
    float release;
    float crossover;
    float sense;
    bool link;
};

// Money for Nothing is a full-scale 1996 master: sample peaks sit on 0 dBFS and
// the 20 ms peak follower lives around -13 dB. A -18 dB target therefore ducks
// the whole song. This program only trims hits that reach -6 dB, with no boost
// and a slow release, so the riff is not ridden.
const Program programs[] = {
    { "Default", -18.0f, 6.0f, 24.0f, 0.00f, -42.0f, 0.50f, 1.5f, -18.0f, 6.0f, 500.0f, 1.0f, 0.50f, -24.0f, 300.0f, 300.0f, -45.0f, true },
    { "Money for Nothing", -6.0f, 0.0f, 3.0f, 0.00f, -30.0f, 0.00f, 1.5f, -18.0f, 0.0f, 800.0f, 1.0f, 0.00f, 0.0f, 1000.0f, 300.0f, -20.0f, true },
};
} // namespace

int Agc2AudioProcessor::getNumPrograms()
{
    return static_cast<int>(std::size(programs));
}

int Agc2AudioProcessor::getCurrentProgram()
{
    return currentProgram;
}

void Agc2AudioProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;

    currentProgram = index;
    applyProgram(index);
}

const juce::String Agc2AudioProcessor::getProgramName(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return {};

    return programs[index].name;
}

void Agc2AudioProcessor::changeProgramName(int, const juce::String&) {}

void Agc2AudioProcessor::applyProgram(int index)
{
    const Program& program = programs[index];

    auto setFloat = [this](const char* id, float value)
    {
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };

    setFloat("target", program.target);
    setFloat("maxBoost", program.maxBoost);
    setFloat("maxCut", program.maxCut);
    setFloat("duck", program.duck);
    setFloat("vad", program.vad);
    setFloat("speed", program.speed);
    setFloat("lookahead", program.lookahead);
    setFloat("targetLoudness", program.targetLoudness);
    setFloat("maxGainBoost", program.maxGainBoost);
    setFloat("baseRelease", program.baseRelease);
    setFloat("peakLookahead", program.peakLookahead);
    setFloat("duckDepth", program.depth);
    setFloat("scThreshold", program.threshold);
    setFloat("scRelease", program.release);
    setFloat("crossover", program.crossover);
    setFloat("scVad", program.sense);
    setFloat("link", program.link ? 1.0f : 0.0f);
    setFloat("bypass", 0.0f);
}

void Agc2AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("program", currentProgram, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void Agc2AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            currentProgram = juce::jlimit(0, getNumPrograms() - 1,
                                           static_cast<int>(apvts.state.getProperty("program", 0)));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Agc2AudioProcessor();
}
