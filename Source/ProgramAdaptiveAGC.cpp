#include "ProgramAdaptiveAGC.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double butterworthQ = 0.7071067811865476;
constexpr float ceilingDb = -0.1f;

double timeCoefficient(double seconds, double rate)
{
    if (seconds <= 0.0 || rate <= 0.0)
        return 1.0;
    return 1.0 - std::exp(-1.0 / (seconds * rate));
}

float decibelPower(double power)
{
    return static_cast<float>(10.0 * std::log10(std::max(power, 1.0e-16)));
}

float linearFromDb(float db)
{
    return std::exp(db * 0.11512925465f);
}
} // namespace

float ProgramAdaptiveAGC::Biquad::process(float input) noexcept
{
    const double x = static_cast<double>(input);
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<float>(y);
}

void ProgramAdaptiveAGC::Split::reset() noexcept
{
    lowA.reset();
    lowB.reset();
    highA.reset();
    highB.reset();
}

void ProgramAdaptiveAGC::Split::process(float input, float& low, float& high) noexcept
{
    low = lowB.process(lowA.process(input));
    high = highB.process(highA.process(input));
}

int ProgramAdaptiveAGC::latencySamplesFor(double rate, float lookaheadMs)
{
    if (rate <= 0.0)
        return 1;
    const float ms = std::clamp(lookaheadMs, 0.5f, 3.0f);
    int samples = static_cast<int>(std::llround(static_cast<double>(ms) * 0.001 * rate));
    samples = std::clamp(samples, 1, Channel::delayCapacity - 1);
    return samples;
}

void ProgramAdaptiveAGC::setButterworth(Biquad& filter, bool highpass, double frequency)
{
    const double clamped = std::clamp(frequency, 20.0, sampleRate * 0.45);
    const double omega = 2.0 * pi * clamped / sampleRate;
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

void ProgramAdaptiveAGC::tuneSplit(Split& split, double frequency)
{
    setButterworth(split.lowA, false, frequency);
    setButterworth(split.lowB, false, frequency);
    setButterworth(split.highA, true, frequency);
    setButterworth(split.highB, true, frequency);
}

void ProgramAdaptiveAGC::updateCrossovers(bool telephone)
{
    const float lowHz = telephone ? 150.0f : crossoverHz;
    const float highHz = telephone ? 3500.0f : 3400.0f;
    if (telephone == telephoneSplit && std::abs(lowHz - tunedLowHz) < 0.5f && std::abs(highHz - tunedHighHz) < 0.5f)
        return;

    telephoneSplit = telephone;
    tunedLowHz = lowHz;
    tunedHighHz = highHz;
    for (int channel = 0; channel < maxChannels; ++channel)
    {
        tuneSplit(channels[channel].lowSplit, lowHz);
        tuneSplit(channels[channel].highSplit, highHz);
    }
}

void ProgramAdaptiveAGC::updateLatency()
{
    latency = latencySamplesFor(sampleRate, lookaheadMs);
}

void ProgramAdaptiveAGC::prepare(double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    preparedChannels = std::clamp(numChannels, 0, maxChannels);
    classifier.prepare(sampleRate);
    classifier.setVadThresholdDecibels(vadThresholdDb);
    updateLatency();
    tunedLowHz = tunedHighHz = -1.0f;
    telephoneSplit = false;
    updateCrossovers(false);
    silenceHold = std::max(1, static_cast<int>(std::llround(0.400 * sampleRate)));
    reset();
}

void ProgramAdaptiveAGC::reset()
{
    classifier.reset();
    for (auto& channel : channels)
    {
        channel.lowSplit.reset();
        channel.highSplit.reset();
        channel.writeIndex = 0;
        std::fill(channel.delay, channel.delay + Channel::delayCapacity, 0.0f);
    }
    for (double& power : bandPower)
        power = 0.0;
    for (float& gain : bandGainDb)
        gain = 0.0f;
    limitGain = 1.0f;
    appliedGainDb = 0.0f;
    limiting = false;
    silenceSamples = 0;
}

void ProgramAdaptiveAGC::setTargetDecibels(float db) { targetDb = db; }

void ProgramAdaptiveAGC::setMaxBoostDecibels(float db)
{
    maxBoostDb = std::clamp(db, 0.0f, 12.0f);
}

void ProgramAdaptiveAGC::setReleaseMilliseconds(float ms)
{
    baseReleaseMs = std::clamp(ms, 100.0f, 1500.0f);
}

void ProgramAdaptiveAGC::setLookaheadMilliseconds(float ms)
{
    lookaheadMs = std::clamp(ms, 0.5f, 3.0f);
    if (sampleRate > 0.0)
        updateLatency();
}

void ProgramAdaptiveAGC::setCrossoverFrequency(float hz)
{
    crossoverHz = std::clamp(hz, 100.0f, 1000.0f);
}

void ProgramAdaptiveAGC::setDuck(float amount)
{
    duck = std::clamp(amount, 0.0f, 1.0f);
}

void ProgramAdaptiveAGC::setVadThresholdDecibels(float db)
{
    vadThresholdDb = db;
    classifier.setVadThresholdDecibels(db);
}

void ProgramAdaptiveAGC::setBypassed(bool shouldBypass) { bypassed = shouldBypass; }

float ProgramAdaptiveAGC::desiredLevelerDb(float levelDb, bool telephoneMid) const
{
    const float target = telephoneMid ? -18.0f : targetDb;
    const float over = levelDb - target;
    if (over >= 0.0f)
    {
        constexpr float knee = 6.0f;
        const float shaped = over < knee ? (over * over / knee) : over;
        const float slope = 1.0f - (1.0f / ratio);
        return -shaped * slope * duck;
    }
    return std::min(-over, boostCap);
}

void ProgramAdaptiveAGC::smoothToward(float& gainDb, float desiredDb, float seconds)
{
    const float coefficient = static_cast<float>(timeCoefficient(static_cast<double>(seconds), sampleRate));
    gainDb += coefficient * (desiredDb - gainDb);
}

void ProgramAdaptiveAGC::process(float* const* channelData, int numChannels, int numSamples)
{
    if (channelData == nullptr || numChannels <= 0 || numSamples <= 0 || preparedChannels <= 0)
        return;

    const int count = std::min(numChannels, preparedChannels);
    const float ceiling = linearFromDb(ceilingDb);

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        float mono = 0.0f;
        float peakNow = 0.0f;
        int contributing = 0;
        for (int channel = 0; channel < count; ++channel)
        {
            if (channelData[channel] == nullptr)
                continue;
            const float input = channelData[channel][sampleIndex];
            mono += input;
            peakNow = std::max(peakNow, std::abs(input));
            ++contributing;
        }
        if (contributing == 0)
            return;
        mono /= static_cast<float>(contributing);

        const AudioClass klass = classifier.process(mono);
        const bool telephone = klass == AudioClass::telephone;
        updateCrossovers(telephone);

        const bool stresser = klass == AudioClass::stresser || classifier.getStationarity() < 0.35f;
        if (klass == AudioClass::silence)
            ++silenceSamples;
        else
            silenceSamples = 0;

        if (bypassed || klass == AudioClass::stresser)
        {
            attackSeconds = 0.005;
            releaseSeconds = 0.2;
        }
        else if (klass == AudioClass::voice)
        {
            ratio = 2.0f;
            boostCap = maxBoostDb;
            attackSeconds = 0.010;
            releaseSeconds = 0.350;
        }
        else if (klass == AudioClass::music)
        {
            ratio = 1.5f;
            boostCap = std::min(maxBoostDb, 6.0f);
            attackSeconds = 0.020;
            releaseSeconds = std::max(baseReleaseMs, 800.0f) * 0.001f;
        }
        else
        {
            ratio = 1.5f;
            boostCap = maxBoostDb;
            attackSeconds = 0.015;
            releaseSeconds = std::max(baseReleaseMs, 500.0f) * 0.001f;
        }
        limitAttackSeconds = stresser ? 0.0002 : 0.005;
        limitReleaseSeconds = stresser ? 0.100 : 0.400;

        float lookPeak = peakNow;
        float bandSample[maxChannels][bandCount];
        for (int channel = 0; channel < count; ++channel)
        {
            if (channelData[channel] == nullptr)
                continue;
            Channel& state = channels[channel];
            if (state.writeIndex >= latency)
                state.writeIndex = 0;
            const float delayed = state.delay[state.writeIndex];
            state.delay[state.writeIndex] = channelData[channel][sampleIndex];
            state.writeIndex = (state.writeIndex + 1) % latency;

            for (int tap = 0; tap < latency; ++tap)
                lookPeak = std::max(lookPeak, std::abs(state.delay[tap]));

            float low = 0.0f;
            float rest = 0.0f;
            state.lowSplit.process(delayed, low, rest);
            float mid = 0.0f;
            float high = 0.0f;
            state.highSplit.process(rest, mid, high);
            bandSample[channel][0] = low;
            bandSample[channel][1] = mid;
            bandSample[channel][2] = high;
        }

        const float powerCoef = static_cast<float>(timeCoefficient(
            klass == AudioClass::voice ? 0.010 : 0.300, sampleRate));
        for (int band = 0; band < bandCount; ++band)
        {
            double energy = 0.0;
            for (int channel = 0; channel < count; ++channel)
            {
                if (channelData[channel] == nullptr)
                    continue;
                const float value = bandSample[channel][band];
                energy += static_cast<double>(value) * value;
            }
            energy /= static_cast<double>(contributing);
            bandPower[band] += static_cast<double>(powerCoef) * (energy - bandPower[band]);
        }

        const bool shortSilence = klass == AudioClass::silence && silenceSamples < silenceHold;
        const bool freezeLeveler = shortSilence || klass == AudioClass::stresser;
        float loudestLinear = 1.0f;
        for (int band = 0; band < bandCount; ++band)
        {
            const float levelDb = decibelPower(bandPower[band]);
            const bool bypassBand = telephone && band != 1;
            float desired = 0.0f;
            if (!bypassBand && levelDb >= vadThresholdDb && !bypassed)
                desired = desiredLevelerDb(levelDb, telephone && band == 1);

            if (bypassed)
                smoothToward(bandGainDb[band], 0.0f, 0.005f);
            else if (klass == AudioClass::silence && !shortSilence)
                smoothToward(bandGainDb[band], 0.0f, 0.250f);
            else if (!freezeLeveler)
            {
                const float seconds = desired < bandGainDb[band] ? static_cast<float>(attackSeconds)
                                                                  : static_cast<float>(releaseSeconds);
                smoothToward(bandGainDb[band], desired, seconds);
            }

            loudestLinear = std::max(loudestLinear, linearFromDb(bandGainDb[band]));
        }

        const float required = lookPeak * loudestLinear;
        float desiredLimit = 1.0f;
        if (required > ceiling && lookPeak > 1.0e-6f)
            desiredLimit = ceiling / required;
        if (bypassed)
            desiredLimit = 1.0f;
        // A mastered peak a fraction of a decibel over the ceiling uses the slow
        // music attack. A boost that would actually clip uses the fast attack,
        // because 1 ms of lookahead cannot finish a 5 ms move.
        const float limitSeconds = desiredLimit < limitGain * 0.89f
            ? 0.0002f
            : (desiredLimit < limitGain ? static_cast<float>(limitAttackSeconds)
                                        : static_cast<float>(limitReleaseSeconds));
        limitGain += static_cast<float>(timeCoefficient(static_cast<double>(limitSeconds), sampleRate))
            * (desiredLimit - limitGain);
        limitGain = std::clamp(limitGain, 0.0f, 1.0f);
        limiting = limitGain < 0.955f;

        float reported = bandGainDb[0];
        for (float gain : bandGainDb)
            reported = std::max(reported, gain);
        reported += 20.0f * std::log10(std::max(limitGain, 1.0e-6f));
        appliedGainDb = reported;

        for (int channel = 0; channel < count; ++channel)
        {
            if (channelData[channel] == nullptr)
                continue;
            float sum = 0.0f;
            for (int band = 0; band < bandCount; ++band)
                sum += bandSample[channel][band] * linearFromDb(bandGainDb[band]);
            channelData[channel][sampleIndex] = sum * limitGain;
        }
    }
}

} // namespace dsp
