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
constexpr double splitHz[3] = { 150.0, 800.0, 3500.0 };

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

void ProgramAdaptiveAGC::tuneAll()
{
    for (int channel = 0; channel < maxChannels; ++channel)
    {
        for (int split = 0; split < splitCount; ++split)
        {
            tuneSplit(channels[channel].audio[split], splitHz[split]);
            tuneSplit(channels[channel].control[split], splitHz[split]);
        }
    }
    splitsTuned = true;
}

void ProgramAdaptiveAGC::splitFour(Split* splits, float input, float* bands) noexcept
{
    float rest = 0.0f;
    splits[0].process(input, bands[0], rest);
    float upper = 0.0f;
    splits[1].process(rest, bands[1], upper);
    splits[2].process(upper, bands[2], bands[3]);
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
    splitsTuned = false;
    tuneAll();
    silenceHold = std::max(1, static_cast<int>(std::llround(0.400 * sampleRate)));
    spikeSamples = std::max(1, static_cast<int>(std::llround(0.012 * sampleRate)));
    reset();
}

void ProgramAdaptiveAGC::reset()
{
    classifier.reset();
    for (auto& channel : channels)
    {
        for (int split = 0; split < splitCount; ++split)
        {
            channel.audio[split].reset();
            channel.control[split].reset();
        }
        channel.writeIndex = 0;
        std::fill(channel.delay, channel.delay + Channel::delayCapacity, 0.0f);
        for (int band = 0; band < bandCount; ++band)
            std::fill(channel.controlBand[band], channel.controlBand[band] + Channel::delayCapacity, 0.0f);
    }
    for (int band = 0; band < bandCount; ++band)
    {
        bandPower[band] = 0.0;
        levelerDb[band] = 0.0f;
        transientGain[band] = 1.0f;
        vcaGain[band] = 1.0f;
        hotSamples[band] = 0;
    }
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
    // The four leveler bands are fixed. This still feeds the sidechain ducker.
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

float ProgramAdaptiveAGC::desiredLevelerDb(float levelDb, int band) const
{
    const bool telephoneBand = telephone && (band == 1 || band == 2);
    const bool telephoneBypass = telephone && (band == 0 || band == 3);
    if (telephoneBypass)
        return 0.0f;
    const float target = telephoneBand ? -18.0f : targetDb;
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

void ProgramAdaptiveAGC::smoothToward(float& value, float target, float seconds)
{
    const float coefficient = static_cast<float>(timeCoefficient(static_cast<double>(seconds), sampleRate));
    value += coefficient * (target - value);
}

void ProgramAdaptiveAGC::process(float* const* channelData, int numChannels, int numSamples)
{
    if (channelData == nullptr || numChannels <= 0 || numSamples <= 0 || preparedChannels <= 0)
        return;
    if (!splitsTuned)
        tuneAll();

    const int count = std::min(numChannels, preparedChannels);
    const float ceiling = linearFromDb(ceilingDb);
    const float zipCoefficient = static_cast<float>(timeCoefficient(0.0005, sampleRate));
    // A deep cut has to finish inside the lookahead. 0.5 ms is the de-zipper
    // for everything else; it is too slow to land a 6 dB drop in 1 ms.
    const float settleCoefficient = static_cast<float>(1.0 - std::exp(std::log(0.03) / static_cast<double>(std::max(latency, 1))));

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        float mono = 0.0f;
        int contributing = 0;
        for (int channel = 0; channel < count; ++channel)
        {
            if (channelData[channel] == nullptr)
                continue;
            mono += channelData[channel][sampleIndex];
            ++contributing;
        }
        if (contributing == 0)
            return;
        mono /= static_cast<float>(contributing);

        const AudioClass klass = classifier.process(mono);
        telephone = klass == AudioClass::telephone;
        if (klass == AudioClass::silence)
            ++silenceSamples;
        else
            silenceSamples = 0;

        if (klass == AudioClass::voice)
        {
            ratio = 2.0f;
            boostCap = maxBoostDb;
            stationaryRelease = 0.300f;
        }
        else if (klass == AudioClass::music)
        {
            ratio = 1.5f;
            boostCap = std::min(maxBoostDb, 6.0f);
            stationaryRelease = std::min(1.5f, std::max(baseReleaseMs, 800.0f) * 0.001f);
        }
        else if (klass == AudioClass::stresser)
        {
            ratio = 1.5f;
            boostCap = maxBoostDb;
            stationaryRelease = 0.300f;
        }
        else
        {
            ratio = 1.5f;
            boostCap = maxBoostDb;
            stationaryRelease = std::clamp(baseReleaseMs, 300.0f, 800.0f) * 0.001f;
        }

        float audioBand[maxChannels][bandCount];
        for (int channel = 0; channel < count; ++channel)
        {
            if (channelData[channel] == nullptr)
                continue;
            Channel& state = channels[channel];
            if (state.writeIndex >= latency)
                state.writeIndex = 0;

            const float delayed = state.delay[state.writeIndex];
            splitFour(state.audio, delayed, audioBand[channel]);

            const float input = channelData[channel][sampleIndex];
            state.delay[state.writeIndex] = input;
            float controlNow[bandCount];
            splitFour(state.control, input, controlNow);
            for (int band = 0; band < bandCount; ++band)
                state.controlBand[band][state.writeIndex] = controlNow[band];
            state.writeIndex = (state.writeIndex + 1) % latency;
        }

        const float powerCoef = static_cast<float>(timeCoefficient(
            klass == AudioClass::voice ? 0.010 : 0.300, sampleRate));
        for (int band = 0; band < bandCount; ++band)
        {
            double energy = 0.0;
            int used = 0;
            for (int channel = 0; channel < count; ++channel)
            {
                if (channelData[channel] == nullptr)
                    continue;
                const int newest = (channels[channel].writeIndex - 1 + latency) % latency;
                const float value = channels[channel].controlBand[band][newest];
                energy += static_cast<double>(value) * value;
                ++used;
            }
            if (used > 0)
                energy /= static_cast<double>(used);
            bandPower[band] += static_cast<double>(powerCoef) * (energy - bandPower[band]);
        }

        const bool shortSilence = klass == AudioClass::silence && silenceSamples < silenceHold;
        const bool longSilence = klass == AudioClass::silence && !shortSilence;
        float levelerLin[bandCount];
        float transTarget[bandCount];
        for (int band = 0; band < bandCount; ++band)
        {
            const float levelDb = decibelPower(bandPower[band]);
            float desired = 0.0f;
            if (levelDb >= vadThresholdDb && !bypassed)
                desired = desiredLevelerDb(levelDb, band);

            if (bypassed)
                smoothToward(levelerDb[band], 0.0f, 0.005f);
            else if (longSilence)
                smoothToward(levelerDb[band], 0.0f, 0.250f);
            else if (!shortSilence)
            {
                const float seconds = desired < levelerDb[band] ? 0.020f : stationaryRelease;
                smoothToward(levelerDb[band], desired, seconds);
            }
            levelerLin[band] = linearFromDb(levelerDb[band]);
            transTarget[band] = 1.0f;
        }

        // Each band may use the whole ceiling on its own, so a bass hit does
        // not take the vocal down with it. If a sample in the window would
        // still sum hot, only the bands that make that sample are trimmed.
        for (int channel = 0; channel < count; ++channel)
        {
            if (channelData[channel] == nullptr)
                continue;
            Channel& state = channels[channel];
            for (int tap = 0; tap < latency; ++tap)
            {
                float predicted = 0.0f;
                float contribution[bandCount];
                for (int band = 0; band < bandCount; ++band)
                {
                    contribution[band] = state.controlBand[band][tap] * levelerLin[band];
                    predicted += contribution[band];
                }
                const float magnitude = std::abs(predicted);
                if (magnitude <= ceiling)
                    continue;
                const float scale = ceiling / magnitude;
                for (int band = 0; band < bandCount; ++band)
                {
                    if (std::abs(contribution[band]) > 0.30f * magnitude)
                        transTarget[band] = std::min(transTarget[band], scale);
                }
            }
        }

        limiting = false;
        float loudestDb = -160.0f;
        for (int band = 0; band < bandCount; ++band)
        {
            if (transTarget[band] < 0.98f)
                ++hotSamples[band];
            const bool spike = hotSamples[band] < spikeSamples;
            if (transTarget[band] < transientGain[band])
                transientGain[band] = transTarget[band];
            else
            {
                const float releaseSeconds = spike ? 0.010f : stationaryRelease;
                smoothToward(transientGain[band], transTarget[band], releaseSeconds);
                if (transientGain[band] > 0.995f)
                    hotSamples[band] = 0;
            }
            transientGain[band] = std::clamp(transientGain[band], 0.0f, 1.0f);
            if (transientGain[band] < 0.97f)
                limiting = true;

            const float desiredLin = levelerLin[band] * transientGain[band];
            const float coefficient = desiredLin < vcaGain[band] * 0.89f ? settleCoefficient : zipCoefficient;
            vcaGain[band] += coefficient * (desiredLin - vcaGain[band]);
            loudestDb = std::max(loudestDb, 20.0f * std::log10(std::max(vcaGain[band], 1.0e-6f)));
        }
        appliedGainDb = loudestDb;

        for (int channel = 0; channel < count; ++channel)
        {
            if (channelData[channel] == nullptr)
                continue;
            float sum = 0.0f;
            for (int band = 0; band < bandCount; ++band)
                sum += audioBand[channel][band] * vcaGain[band];
            channelData[channel][sampleIndex] = sum;
        }
    }
}

} // namespace dsp
