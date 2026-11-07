#pragma once

namespace dsp
{

// Lookahead sidechain ducker. The main path is delayed; the sidechain is read
// at the same moment, with no delay of its own. Highs take the duck. Bass does not.
class PredictiveDucker
{
public:
    void prepare(double sampleRate, int numChannels);
    void reset();

    void setDuckDepth(float amount);
    void setThresholdDecibels(float db);
    void setReleaseMilliseconds(float ms);
    void setCrossoverFrequency(float hz);
    void setVadThresholdDecibels(float db);

    void process(float* const* main, int numMainChannels,
                 const float* const* sidechain, int numSidechainChannels,
                 int numSamples);

    int getLatencySamples() const;

    static int latencySamplesFor(double sampleRate);

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

        void reset() { z1 = z2 = 0.0; }
        float process(float input);
    };

    struct Split
    {
        Biquad lowA;
        Biquad lowB;
        Biquad highA;
        Biquad highB;

        void reset();
        void process(float input, float& low, float& high);
    };

    struct SmootherExponential
    {
        float current = 1.0f;
        float target = 1.0f;
        double coefficient = 1.0;

        void prepare(double sampleRate, double seconds);
        void reset(float value);
        float getCurrent() const { return current; }
        void setTarget(float next) { target = next; }
        float process();
    };

    void updateCoefficients();
    void updateSplitCoefficients();
    void updateDetector(float magnitude);

    static void setButterworth(Biquad& filter, bool highpass, double sampleRate, double frequency);

    double sampleRate = 48000.0;
    float depth = 0.5f;
    float thresholdDb = -24.0f;
    float releaseMs = 300.0f;
    float crossoverHz = 300.0f;
    float vadThresholdDb = -45.0f;

    int preparedChannels = 0;
    int latency = 0;
    int vadHold = 0;
    int vadHoldBudget = 1;
    double peak = 0.0;
    double meanSquare = 0.0;
    double releaseCoefficient = 1.0;
    double rmsCoefficient = 1.0;

    SmootherExponential smoother;

    static constexpr int maxChannels = 8;
    static constexpr int delayCapacity = 512;
    Split bands[maxChannels];
    float delayStorage[maxChannels][delayCapacity] {};
    int writeIndex[maxChannels] {};
};

} // namespace dsp
