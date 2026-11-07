#pragma once

#include "AudioClassifier.h"

namespace dsp
{

// Four-band leveler plus a per-band lookahead limiter. A hit in the bass
// reduces the bass band only. A short spike recovers in about 10 ms. A level
// that stays up uses the slow release. The 0.5 ms smoother is only the
// de-zipper into the multiplier.
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
        Split audio[3];
        Split control[3];
        int writeIndex = 0;
        static constexpr int delayCapacity = 1024;
        float delay[delayCapacity] {};
        float controlBand[4][delayCapacity] {};
    };

    void updateLatency();
    void tuneAll();
    void setButterworth(Biquad& filter, bool highpass, double frequency);
    void tuneSplit(Split& split, double frequency);
    void splitFour(Split* splits, float input, float* bands) noexcept;
    float desiredLevelerDb(float levelDb, int band) const;
    void smoothToward(float& value, float target, float seconds);

    static constexpr int maxChannels = 8;
    static constexpr int bandCount = 4;
    static constexpr int splitCount = 3;

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
    bool splitsTuned = false;

    int preparedChannels = 0;
    int latency = 1;
    int silenceSamples = 0;
    int silenceHold = 1;
    int spikeSamples = 1;

    double bandPower[bandCount] {};
    float levelerDb[bandCount] {};
    float transientGain[bandCount] {};
    float vcaGain[bandCount] {};
    int hotSamples[bandCount] {};
    float appliedGainDb = 0.0f;
    bool limiting = false;

    float ratio = 1.5f;
    float boostCap = 6.0f;
    float stationaryRelease = 0.8f;
    bool telephone = false;
};

} // namespace dsp
