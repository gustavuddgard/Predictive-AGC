#include "PredictiveDucker.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr double butterworthQ = 0.7071067811865476;
constexpr double fullDuckOverDb = 18.0;

double timeCoefficient(double seconds, double sampleRate)
{
    if (seconds <= 0.0 || sampleRate <= 0.0)
        return 1.0;
    return 1.0 - std::exp(-1.0 / (seconds * sampleRate));
}
} // namespace

float PredictiveDucker::Biquad::process(float input)
{
    const double x = static_cast<double>(input);
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<float>(y);
}

void PredictiveDucker::Split::reset()
{
    lowA.reset();
    lowB.reset();
    highA.reset();
    highB.reset();
}

void PredictiveDucker::Split::process(float input, float& low, float& high)
{
    low = lowB.process(lowA.process(input));
    high = highB.process(highA.process(input));
}

void PredictiveDucker::SmootherExponential::prepare(double rate, double seconds)
{
    coefficient = timeCoefficient(seconds, rate);
}

void PredictiveDucker::SmootherExponential::reset(float value)
{
    current = target = value;
}

float PredictiveDucker::SmootherExponential::process()
{
    current += static_cast<float>(coefficient * static_cast<double>(target - current));
    return current;
}

void PredictiveDucker::setButterworth(Biquad& filter, bool highpass, double rate, double frequency)
{
    const double clamped = std::clamp(frequency, 20.0, rate * 0.45);
    const double omega = 2.0 * 3.14159265358979323846 * clamped / rate;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double alpha = sine / (2.0 * butterworthQ);
    const double a0 = 1.0 + alpha;

    if (highpass)
    {
        filter.b0 = ((1.0 + cosine) * 0.5) / a0;
        filter.b1 = (-(1.0 + cosine)) / a0;
        filter.b2 = filter.b0;
    }
    else
    {
        filter.b0 = ((1.0 - cosine) * 0.5) / a0;
        filter.b1 = (1.0 - cosine) / a0;
        filter.b2 = filter.b0;
    }

    filter.a1 = (-2.0 * cosine) / a0;
    filter.a2 = (1.0 - alpha) / a0;
}

int PredictiveDucker::latencySamplesFor(double rate)
{
    if (rate <= 0.0)
        return 1;
    const int samples = static_cast<int>(std::llround(0.001 * rate));
    return std::max(1, samples);
}

void PredictiveDucker::updateSplitCoefficients()
{
    for (int channel = 0; channel < maxChannels; ++channel)
    {
        setButterworth(bands[channel].lowA, false, sampleRate, crossoverHz);
        setButterworth(bands[channel].lowB, false, sampleRate, crossoverHz);
        setButterworth(bands[channel].highA, true, sampleRate, crossoverHz);
        setButterworth(bands[channel].highB, true, sampleRate, crossoverHz);
    }
}

void PredictiveDucker::updateCoefficients()
{
    releaseCoefficient = timeCoefficient(static_cast<double>(releaseMs) * 0.001, sampleRate);
    rmsCoefficient = timeCoefficient(0.015, sampleRate);
    vadHoldBudget = std::max(1, static_cast<int>(std::llround(0.012 * sampleRate)));
    smoother.prepare(sampleRate, 0.0005);
    updateSplitCoefficients();
}

void PredictiveDucker::prepare(double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    preparedChannels = std::clamp(numChannels, 0, maxChannels);
    latency = std::min(latencySamplesFor(sampleRate), delayCapacity);
    updateCoefficients();
    reset();
}

void PredictiveDucker::reset()
{
    peak = 0.0;
    meanSquare = 0.0;
    vadHold = 0;
    smoother.reset(1.0f);
    for (int channel = 0; channel < maxChannels; ++channel)
    {
        bands[channel].reset();
        writeIndex[channel] = 0;
        std::fill(delayStorage[channel], delayStorage[channel] + delayCapacity, 0.0f);
    }
}

void PredictiveDucker::setDuckDepth(float amount)
{
    depth = std::clamp(amount, 0.0f, 1.0f);
}

void PredictiveDucker::setThresholdDecibels(float db)
{
    thresholdDb = std::clamp(db, -80.0f, 0.0f);
}

void PredictiveDucker::setReleaseMilliseconds(float ms)
{
    releaseMs = std::clamp(ms, 10.0f, 2000.0f);
    if (sampleRate > 0.0)
        releaseCoefficient = timeCoefficient(static_cast<double>(releaseMs) * 0.001, sampleRate);
}

void PredictiveDucker::setCrossoverFrequency(float hz)
{
    const float clamped = std::clamp(hz, 40.0f, 4000.0f);
    if (std::abs(clamped - crossoverHz) < 0.25f)
        return;
    crossoverHz = clamped;
    if (sampleRate > 0.0)
        updateSplitCoefficients();
}

void PredictiveDucker::setVadThresholdDecibels(float db)
{
    vadThresholdDb = std::clamp(db, -80.0f, 0.0f);
}

int PredictiveDucker::getLatencySamples() const
{
    return latency;
}

void PredictiveDucker::updateDetector(float magnitude)
{
    const double mag = static_cast<double>(std::max(0.0f, magnitude));
    const double vadLinear = std::pow(10.0, static_cast<double>(vadThresholdDb) / 20.0);
    meanSquare += rmsCoefficient * (mag * mag - meanSquare);
    const double rms = std::sqrt(std::max(meanSquare, 0.0));

    if (mag >= vadLinear || rms >= vadLinear)
        vadHold = vadHoldBudget;
    else if (vadHold > 0)
        --vadHold;

    const bool voice = vadHold > 0;
    if (!voice)
    {
        // Silence cannot start a duck. Once the peak has fallen under the
        // threshold, hold the gain so the noise floor does not wobble it.
        peak += releaseCoefficient * (0.0 - peak);
        const double peakDb = 20.0 * std::log10(std::max(peak, 1.0e-8));
        if (peakDb <= static_cast<double>(thresholdDb))
        {
            smoother.setTarget(smoother.getCurrent());
            return;
        }
    }
    else if (mag > peak)
    {
        peak = mag;
    }
    else
    {
        peak += releaseCoefficient * (mag - peak);
    }

    const double peakDb = 20.0 * std::log10(std::max(peak, 1.0e-8));
    const double over = peakDb - static_cast<double>(thresholdDb);
    float target = 1.0f;
    if (over > 0.0)
    {
        const double amount = std::min(1.0, over / fullDuckOverDb);
        target = static_cast<float>(1.0 - static_cast<double>(depth) * amount);
    }
    smoother.setTarget(std::clamp(target, 0.0f, 1.0f));
}

void PredictiveDucker::process(float* const* main, int numMainChannels,
                               const float* const* sidechain, int numSidechainChannels,
                               int numSamples)
{
    if (main == nullptr || numMainChannels <= 0 || numSamples <= 0 || preparedChannels <= 0 || latency <= 0)
        return;

    const int channels = std::min(numMainChannels, preparedChannels);
    const int sides = sidechain == nullptr ? 0 : std::max(0, numSidechainChannels);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float sidePeak = 0.0f;
        for (int channel = 0; channel < sides; ++channel)
        {
            if (sidechain[channel] != nullptr)
                sidePeak = std::max(sidePeak, std::abs(sidechain[channel][sample]));
        }

        updateDetector(sidePeak);
        const float gain = smoother.process();

        for (int channel = 0; channel < channels; ++channel)
        {
            if (main[channel] == nullptr)
                continue;

            const int index = writeIndex[channel];
            const float delayed = delayStorage[channel][index];
            delayStorage[channel][index] = main[channel][sample];
            writeIndex[channel] = (index + 1) % latency;

            float low = 0.0f;
            float high = 0.0f;
            bands[channel].process(delayed, low, high);
            main[channel][sample] = low + high * gain;
        }
    }
}

} // namespace dsp
