#include "AudioClassifier.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double butterworthQ = 0.7071067811865476;

double onePole(double seconds, double rate)
{
    if (seconds <= 0.0 || rate <= 0.0)
        return 1.0;
    return 1.0 - std::exp(-1.0 / (seconds * rate));
}

float decibels(double power)
{
    return static_cast<float>(10.0 * std::log10(std::max(power, 1.0e-16)));
}
} // namespace

float AudioClassifier::Biquad::process(float input) noexcept
{
    const double x = static_cast<double>(input);
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<float>(y);
}

void AudioClassifier::designBandpass(Biquad& filter, double frequency, double q)
{
    const double clamped = std::clamp(frequency, 5.0, sampleRate * 0.45);
    const double omega = 2.0 * pi * clamped / sampleRate;
    const double sine = std::sin(omega);
    const double cosine = std::cos(omega);
    const double alpha = sine / (2.0 * std::max(q, 0.1));
    const double a0 = 1.0 + alpha;
    filter.b0 = alpha / a0;
    filter.b1 = 0.0;
    filter.b2 = -alpha / a0;
    filter.a1 = (-2.0 * cosine) / a0;
    filter.a2 = (1.0 - alpha) / a0;
}

void AudioClassifier::designHighpass(Biquad& filter, double frequency)
{
    const double clamped = std::clamp(frequency, 20.0, sampleRate * 0.45);
    const double omega = 2.0 * pi * clamped / sampleRate;
    const double sine = std::sin(omega);
    const double cosine = std::cos(omega);
    const double alpha = sine / (2.0 * butterworthQ);
    const double a0 = 1.0 + alpha;
    filter.b0 = ((1.0 + cosine) * 0.5) / a0;
    filter.b1 = (-(1.0 + cosine)) / a0;
    filter.b2 = filter.b0;
    filter.a1 = (-2.0 * cosine) / a0;
    filter.a2 = (1.0 - alpha) / a0;
}

void AudioClassifier::designLowpass(Biquad& filter, double frequency)
{
    const double clamped = std::clamp(frequency, 20.0, sampleRate * 0.45);
    const double omega = 2.0 * pi * clamped / sampleRate;
    const double sine = std::sin(omega);
    const double cosine = std::cos(omega);
    const double alpha = sine / (2.0 * butterworthQ);
    const double a0 = 1.0 + alpha;
    filter.b0 = ((1.0 - cosine) * 0.5) / a0;
    filter.b1 = (1.0 - cosine) / a0;
    filter.b2 = filter.b0;
    filter.a1 = (-2.0 * cosine) / a0;
    filter.a2 = (1.0 - alpha) / a0;
}

void AudioClassifier::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const double centers[bandCount] = { 80.0, 200.0, 500.0, 1200.0, 3000.0, 8000.0 };
    for (int band = 0; band < bandCount; ++band)
        designBandpass(bands[band], centers[band], 1.2);
    designHighpass(phoneHigh, 300.0);
    designLowpass(phoneLow, 3400.0);
    designBandpass(syllable, 4.0, 1.0);

    powerCoef = onePole(0.030, sampleRate);
    fastCoef = onePole(0.010, sampleRate);
    slowCoef = onePole(0.300, sampleRate);
    peakCoef = onePole(0.005, sampleRate);
    envelopeCoef = onePole(0.010, sampleRate);
    syllableCoef = onePole(0.080, sampleRate);
    varianceCoef = onePole(0.300, sampleRate);
    holeCoef = onePole(0.300, sampleRate);
    hangSamples = std::max(1, static_cast<int>(std::llround(0.080 * sampleRate)));
    reset();
}

void AudioClassifier::reset()
{
    for (auto& band : bands)
        band.reset();
    phoneHigh.reset();
    phoneLow.reset();
    syllable.reset();
    for (double& power : bandPower)
        power = 0.0;
    phonePower = totalPower = fastPower = slowPower = 0.0;
    peak = envelope = syllablePower = 0.0;
    levelMean = -60.0;
    levelSquare = 3600.0;
    std::fill(pitch, pitch + pitchCapacity, 0.0f);
    pitchWrite = 0;
    pitchSamples = 0;
    pitchScore = 0.0f;
    entropy = 0.0f;
    crestDb = 0.0f;
    bandwidthRatio = 0.0f;
    syllableAmount = 0.0f;
    stationarity = 1.0f;
    hole = 0.0f;
    fastDb = slowDb = -160.0f;
    current = pending = AudioClass::silence;
    pendingSamples = 0;
}

void AudioClassifier::setVadThresholdDecibels(float db)
{
    vadThresholdDb = db;
}

void AudioClassifier::updatePitch()
{
    const int window = std::min(1024, static_cast<int>(pitchCapacity));
    const int minLag = std::max(1, static_cast<int>(sampleRate / 250.0));
    const int maxLag = std::min(window - 1, static_cast<int>(sampleRate / 50.0));
    if (maxLag <= minLag)
    {
        pitchScore = 0.0f;
        return;
    }

    double energy = 0.0;
    for (int i = 0; i < window; ++i)
    {
        const int index = (pitchWrite - 1 - i + pitchCapacity) & (pitchCapacity - 1);
        const double value = static_cast<double>(pitch[index]);
        energy += value * value;
    }
    if (energy < 1.0e-8)
    {
        pitchScore = 0.0f;
        return;
    }

    double best = 0.0;
    for (int lag = minLag; lag <= maxLag; lag += 4)
    {
        double correlation = 0.0;
        for (int i = 0; i < window - lag; i += 2)
        {
            const int a = (pitchWrite - 1 - i + pitchCapacity) & (pitchCapacity - 1);
            const int b = (pitchWrite - 1 - i - lag + pitchCapacity) & (pitchCapacity - 1);
            correlation += static_cast<double>(pitch[a]) * static_cast<double>(pitch[b]);
        }
        best = std::max(best, correlation / energy);
    }
    pitchScore = static_cast<float>(std::clamp(best, 0.0, 1.0));
}

AudioClass AudioClassifier::decide() const
{
    // The fast window is the silence gate. The slow window stays hot for seconds
    // after a burst, which would pin a cut or a boost onto the next program.
    if (fastDb < vadThresholdDb)
        return AudioClass::silence;

    // A mastered mix is loud and full-range, and a snare has crest. Neither is a
    // stresser. That label needs a flat spectrum, gaps in the recent level, and
    // no pitch, which is what a burst of noise between digital holes looks like.
    const bool stresser = entropy > 0.70f && hole > 0.22f && pitchScore < 0.35f;
    if (stresser)
        return AudioClass::stresser;
    if (bandwidthRatio > 0.90f && entropy < 0.80f)
        return AudioClass::telephone;
    if (syllableAmount > 0.18f && pitchScore > 0.45f)
        return AudioClass::voice;
    return AudioClass::music;
}

AudioClass AudioClassifier::process(float sample)
{
    const float magnitude = std::abs(sample);
    totalPower += powerCoef * (static_cast<double>(sample) * sample - totalPower);
    fastPower += fastCoef * (static_cast<double>(magnitude) * magnitude - fastPower);
    slowPower += slowCoef * (static_cast<double>(magnitude) * magnitude - slowPower);
    if (static_cast<double>(magnitude) > peak)
        peak = magnitude;
    else
        peak += peakCoef * (static_cast<double>(magnitude) - peak);

    fastDb = decibels(fastPower);
    slowDb = decibels(slowPower);
    const float quiet = fastDb < vadThresholdDb ? 1.0f : 0.0f;
    hole += static_cast<float>(holeCoef) * (quiet - hole);
    const float peakDb = static_cast<float>(20.0 * std::log10(std::max(peak, 1.0e-8)));
    crestDb = peakDb - slowDb;

    // Constant-Q bands get wider as they go up, so white noise would look tonal.
    // Divide by the centre frequency before the entropy sum.
    static const double centers[bandCount] = { 80.0, 200.0, 500.0, 1200.0, 3000.0, 8000.0 };
    double bandSum = 0.0;
    double density[bandCount];
    for (int band = 0; band < bandCount; ++band)
    {
        const float filtered = bands[band].process(sample);
        bandPower[band] += powerCoef * (static_cast<double>(filtered) * filtered - bandPower[band]);
        density[band] = bandPower[band] / centers[band];
        bandSum += density[band];
    }
    float entropySum = 0.0f;
    if (bandSum > 1.0e-16)
    {
        for (double power : density)
        {
            const float probability = static_cast<float>(power / bandSum);
            if (probability > 1.0e-8f)
                entropySum -= probability * std::log(probability);
        }
    }
    entropy = entropySum / static_cast<float>(std::log(static_cast<double>(bandCount)));

    const float phone = phoneLow.process(phoneHigh.process(sample));
    phonePower += powerCoef * (static_cast<double>(phone) * phone - phonePower);
    bandwidthRatio = static_cast<float>(phonePower / std::max(totalPower, 1.0e-16));
    bandwidthRatio = std::clamp(bandwidthRatio, 0.0f, 1.0f);

    envelope += envelopeCoef * (static_cast<double>(magnitude) - envelope);
    const float shaped = syllable.process(static_cast<float>(envelope));
    syllablePower += syllableCoef * (static_cast<double>(shaped) * shaped - syllablePower);
    syllableAmount = static_cast<float>(std::sqrt(std::max(syllablePower, 0.0)) / std::max(envelope, 1.0e-6));
    syllableAmount = std::clamp(syllableAmount, 0.0f, 4.0f);

    levelMean += varianceCoef * (static_cast<double>(slowDb) - levelMean);
    const double deviation = static_cast<double>(slowDb) - levelMean;
    levelSquare += varianceCoef * (deviation * deviation - levelSquare);
    stationarity = static_cast<float>(1.0 / (1.0 + std::sqrt(std::max(levelSquare, 0.0)) / 8.0));

    pitch[pitchWrite & (pitchCapacity - 1)] = sample;
    pitchWrite = (pitchWrite + 1) & (pitchCapacity - 1);
    if ((++pitchSamples & 511) == 0)
        updatePitch();

    const AudioClass raw = decide();
    if (raw == AudioClass::silence)
    {
        current = pending = raw;
        pendingSamples = 0;
    }
    else if (raw == current)
    {
        pending = raw;
        pendingSamples = 0;
    }
    else if (raw == pending)
    {
        if (++pendingSamples >= hangSamples)
            current = raw;
    }
    else
    {
        pending = raw;
        pendingSamples = 0;
    }
    return current;
}

} // namespace dsp
