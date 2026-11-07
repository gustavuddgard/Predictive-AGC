#pragma once

namespace dsp
{

// Feedforward leveler. A baked model decides when to engage. The delay is the
// lookahead control, from 1.5 ms to 40 ms, so a move can finish before the
// audio is heard. No JUCE types in here.
class PredictiveAGC
{
public:
    void prepare(double sampleRate, int numChannels);
    void process(float* const* channelData, int numChannels, int numSamples);
    void reset();

    void setTargetDecibels(float db);
    void setMaxBoostDecibels(float db);
    void setMaxCutDecibels(float db);
    void setVadThresholdDecibels(float db);
    void setDuck(float amount);
    void setSpeed(float normalised);
    void setLookaheadMilliseconds(float ms);
    void setLinked(bool shouldLink);
    void setBypassed(bool shouldBypass);

    float getGainDecibels() const;
    float getEngageProbability() const;
    bool isEngaged() const;
    int getLatencySamples() const;

    static int latencySamplesFor(double sampleRate);
    static int latencySamplesFor(double sampleRate, float lookaheadMs);

private:
    struct Channel
    {
        double peak = 0.0;
        double meanSquare = 0.0;
        double levelDb = -160.0;
        double previousLevelDb = -160.0;
        double slope = 0.0;
        double previousSlope = 0.0;
        double acceleration = 0.0;
        double gainDb = 0.0;
        float probability = 0.0f;
        int hangLeft = 0;
        int sampleCount = 0;
        int writeIndex = 0;
        bool latched = false;
        bool releaseArmed = true;
        bool vadOpen = false;
        int vadHoldSamples = 0;
        static constexpr int delayCapacity = 8192;
        float delayStorage[delayCapacity] {};
    };

    void updateCoefficients();
    void updateDetector(Channel& channel, float magnitude);
    void updateGain(Channel& channel);
    double desiredGain(double levelDb) const;
    float readDelay(Channel& channel, float input);

    static double timeCoef(double seconds, double sampleRate);
    static float modelProbability(const float* features);

    double sampleRate = 48000.0;
    float targetDb = -18.0f;
    float maxBoostDb = 6.0f;
    float maxCutDb = 24.0f;
    float vadThresholdDb = -42.0f;
    float duck = 0.4f;
    float speed = 0.5f;
    float lookaheadMs = 1.5f;
    bool linked = true;
    bool bypassed = false;

    int preparedChannels = 0;
    int latency = 0;

    double peakReleaseCoef = 0.0;
    double rmsCoef = 0.0;
    double slopeCoef = 0.0;
    double accelCoef = 0.0;
    double attackCoef = 0.0;
    double releaseCoef = 0.0;
    double bypassCoef = 0.0;
    int hangSamples = 0;
    int vadHoldBudget = 1;

    static constexpr int maxChannels = 8;
    Channel channels[maxChannels];
};

} // namespace dsp
