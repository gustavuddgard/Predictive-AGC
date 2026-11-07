#pragma once

namespace dsp
{

enum class AudioClass
{
    silence = 0,
    music = 1,
    voice = 2,
    telephone = 3,
    stresser = 4
};

// Real-time program classifier. It reads the undelayed waveform. No FFT:
// bandpass energies, a 4 Hz envelope, and a short autocorrelation.
class AudioClassifier
{
public:
    void prepare(double sampleRate);
    void reset();
    void setVadThresholdDecibels(float db);

    AudioClass process(float sample);

    AudioClass getClass() const noexcept { return current; }
    float getStationarity() const noexcept { return stationarity; }
    float getEntropy() const noexcept { return entropy; }
    float getCrestDecibels() const noexcept { return crestDb; }
    bool isSilent() const noexcept { return current == AudioClass::silence; }

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

    void designBandpass(Biquad& filter, double frequency, double q);
    void designHighpass(Biquad& filter, double frequency);
    void designLowpass(Biquad& filter, double frequency);
    void updatePitch();
    AudioClass decide() const;

    static constexpr int bandCount = 6;
    static constexpr int pitchCapacity = 2048;

    double sampleRate = 48000.0;
    float vadThresholdDb = -42.0f;

    Biquad bands[bandCount];
    Biquad phoneHigh;
    Biquad phoneLow;
    Biquad syllable;

    double bandPower[bandCount] {};
    double phonePower = 0.0;
    double totalPower = 0.0;
    double fastPower = 0.0;
    double slowPower = 0.0;
    double peak = 0.0;
    double envelope = 0.0;
    double syllablePower = 0.0;
    double levelMean = -60.0;
    double levelSquare = 3600.0;

    double powerCoef = 0.0;
    double fastCoef = 0.0;
    double slowCoef = 0.0;
    double peakCoef = 0.0;
    double envelopeCoef = 0.0;
    double syllableCoef = 0.0;
    double varianceCoef = 0.0;

    float pitch[pitchCapacity] {};
    int pitchWrite = 0;
    int pitchSamples = 0;
    float pitchScore = 0.0f;

    float entropy = 0.0f;
    float crestDb = 0.0f;
    float bandwidthRatio = 0.0f;
    float syllableAmount = 0.0f;
    float stationarity = 1.0f;
    float fastDb = -160.0f;
    float slowDb = -160.0f;

    AudioClass current = AudioClass::silence;
    AudioClass pending = AudioClass::silence;
    int pendingSamples = 0;
    int hangSamples = 1;
};

} // namespace dsp
