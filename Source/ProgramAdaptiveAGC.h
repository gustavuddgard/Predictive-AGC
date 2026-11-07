#pragma once

#include "AudioClassifier.h"

namespace dsp
{

// Slow per-band leveler plus a lookahead peak guard. The classifier picks the
// ballistics. The peak guard is the only stage allowed to cut a full-scale burst,
// and it releases on its own so that cut does not ride into the next program.
class ProgramAdaptiveAGC
{
public:
    void prepare(double sampleRate, int numChannels);
    void process(float* const* channelData, int numChannels, int numSamples);
    void reset();

    void setTargetDecibels(float db);
    void setMaxBoostDecibels(float db);
    void setReleaseMilliseconds(float ms);
    void setLookaheadMilliseconds(float ms);
    void setCrossoverFrequency(float hz);
    void setDuck(float amount);
    void setVadThresholdDecibels(float db);
    void setBypassed(bool shouldBypass);

    float getGainDecibels() const noexcept { return appliedGainDb; }
    bool isLimiting() const noexcept { return limiting; }
    AudioClass getClass() const noexcept { return classifier.getClass(); }
    int getLatencySamples() const noexcept { return latency; }

    static int latencySamplesFor(double sampleRate, float lookaheadMs);

private:
    struct Biquad
    {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
        double z1 = 0.0;
        double z2 = 0.0;

        void reset() noexcept { z1 = z2 = 0.0; }
        float process(float input) noexcept;
    };

    struct Split
    {
        Biquad lowA;
        Biquad lowB;
        Biquad highA;
        Biquad highB;

        void reset() noexcept;
        void process(float input, float& low, float& high) noexcept;
    };

    struct Channel
    {
        Split lowSplit;
        Split highSplit;
        int writeIndex = 0;
        static constexpr int delayCapacity = 1024;
        float delay[delayCapacity] {};
    };

    void updateLatency();
    void updateCrossovers(bool telephone);
    void setButterworth(Biquad& filter, bool highpass, double frequency);
    void tuneSplit(Split& split, double frequency);
    float desiredLevelerDb(float levelDb, bool telephoneMid) const;
    void smoothToward(float& gainDb, float desiredDb, float seconds);

    static constexpr int maxChannels = 8;
    static constexpr int bandCount = 3;

    AudioClassifier classifier;
    Channel channels[maxChannels];

    double sampleRate = 48000.0;
    float targetDb = -18.0f;
    float maxBoostDb = 6.0f;
    float baseReleaseMs = 500.0f;
    float lookaheadMs = 1.0f;
    float crossoverHz = 300.0f;
    float duck = 0.0f;
    float vadThresholdDb = -42.0f;
    bool bypassed = false;
    bool telephoneSplit = false;
    float tunedLowHz = -1.0f;
    float tunedHighHz = -1.0f;

    int preparedChannels = 0;
    int latency = 1;
    int silenceSamples = 0;
    int silenceHold = 1;

    double bandPower[bandCount] {};
    float bandGainDb[bandCount] {};
    float limitGain = 1.0f;
    float appliedGainDb = 0.0f;
    bool limiting = false;

    double attackSeconds = 0.005;
    double releaseSeconds = 0.8;
    double limitAttackSeconds = 0.005;
    double limitReleaseSeconds = 0.4;
    float ratio = 1.5f;
    float boostCap = 6.0f;
};

} // namespace dsp
