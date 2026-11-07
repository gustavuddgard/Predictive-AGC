#pragma once

namespace dsp
{

// Feedforward leveler. A baked model decides when to engage; a 1.5 ms delay
// gives that decision time to land on the audio. No JUCE types in here.
class PredictiveAGC
{
public:
    void prepare(double sampleRate, int numChannels);
    void process(float* const* channelData, int numChannels, int numSamples);
    void reset();

    void setTargetDecibels(float db);
    void setMaxBoostDecibels(float db);
    void setMaxCutDecibels(float db);
    void setSpeed(float normalised);
    void setLinked(bool shouldLink);
    void setBypassed(bool shouldBypass);

    float getGainDecibels() const;
    float getEngageProbability() const;
    bool isEngaged() const;
    int getLatencySamples() const;

    static int latencySamplesFor(double sampleRate);

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
        static constexpr int delayCapacity = 1024;
        float delayStorage[delayCapacity] {};
    };

    void updateCoefficients();
    void updateDetector(Channel& channel, float magnitude);
    void updateGain(Channel& channel);
    float readDelay(Channel& channel, float input);

    static double timeCoef(double seconds, double sampleRate);
    static float modelProbability(const float features[6]);

    double sampleRate = 48000.0;
    float targetDb = -18.0f;
    float maxBoostDb = 12.0f;
    float maxCutDb = 24.0f;
    float speed = 0.5f;
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
    double silenceCoef = 0.0;
    double bypassCoef = 0.0;
    int hangSamples = 0;

    static constexpr int maxChannels = 8;
    Channel channels[maxChannels];
};

} // namespace dsp
