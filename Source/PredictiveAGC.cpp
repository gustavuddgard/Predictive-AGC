#include "PredictiveAGC.h"
#include "EngageWeights.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr double gateDb = -60.0;
constexpr double kneeDb = 6.0;
constexpr float maxBoostCeilingDb = 6.0f;
constexpr float engageOn = 0.6f;
constexpr float engageOff = 0.4f;
constexpr int inferInterval = 8;

float clampedFeature(double value, double scale)
{
    const double scaled = value / scale;
    return static_cast<float>(std::clamp(scaled, -3.0, 3.0));
}
} // namespace

double PredictiveAGC::timeCoef(double seconds, double rate)
{
    if (seconds <= 0.0 || rate <= 0.0)
        return 1.0;
    return 1.0 - std::exp(-1.0 / (seconds * rate));
}

int PredictiveAGC::latencySamplesFor(double rate)
{
    if (rate <= 0.0)
        return 1;

    int samples = static_cast<int>(std::llround(0.0015 * rate));
    if (samples < 1)
        samples = 1;

    while (samples > 1 && (static_cast<double>(samples) / rate) >= 0.002)
        --samples;

    return samples;
}

float PredictiveAGC::modelProbability(const float features[6])
{
    const auto squash = [](float z)
    {
        z = std::clamp(z, -20.0f, 20.0f);
        return 1.0f / (1.0f + std::exp(-z));
    };

    if constexpr (EngageWeights::kind == EngageModelKind::logistic)
    {
        float z = EngageWeights::logisticB;
        for (int i = 0; i < EngageWeights::inputCount; ++i)
            z += EngageWeights::logisticW[i] * features[i];
        return squash(z);
    }
    else
    {
        float hidden[EngageWeights::hiddenCount];
        for (int unit = 0; unit < EngageWeights::hiddenCount; ++unit)
        {
            float sum = EngageWeights::mlpB1[unit];
            const int row = unit * EngageWeights::inputCount;
            for (int i = 0; i < EngageWeights::inputCount; ++i)
                sum += EngageWeights::mlpW1[row + i] * features[i];
            hidden[unit] = std::tanh(sum);
        }

        float z = EngageWeights::mlpB2;
        for (int unit = 0; unit < EngageWeights::hiddenCount; ++unit)
            z += EngageWeights::mlpW2[unit] * hidden[unit];
        return squash(z);
    }
}

void PredictiveAGC::updateCoefficients()
{
    // Speed 0.5 is a 400 ms release. The fast end is 200 ms, so recovery between
    // words cannot snap back the way an 80 ms release does.
    const float releaseMs = 400.0f * std::pow(2.0f, 1.0f - 2.0f * speed);
    const double hangMs = 20.0 * (1.0 - static_cast<double>(speed));

    peakReleaseCoef = timeCoef(0.020, sampleRate);
    rmsCoef = timeCoef(0.005, sampleRate);
    slopeCoef = timeCoef(0.0005, sampleRate);
    accelCoef = timeCoef(0.0005, sampleRate);
    attackCoef = timeCoef(0.015, sampleRate);
    releaseCoef = timeCoef(static_cast<double>(releaseMs) * 0.001, sampleRate);
    bypassCoef = timeCoef(0.005, sampleRate);
    hangSamples = static_cast<int>(std::llround(hangMs * 0.001 * sampleRate));
    vadHoldBudget = std::max(1, static_cast<int>(std::llround(0.012 * sampleRate)));
}

void PredictiveAGC::prepare(double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    preparedChannels = std::clamp(numChannels, 0, maxChannels);
    latency = latencySamplesFor(sampleRate);
    if (latency > Channel::delayCapacity)
        latency = Channel::delayCapacity;

    updateCoefficients();
    reset();
}

void PredictiveAGC::reset()
{
    for (int ch = 0; ch < maxChannels; ++ch)
        channels[ch] = Channel{};
}

void PredictiveAGC::setTargetDecibels(float db) { targetDb = db; }

void PredictiveAGC::setMaxBoostDecibels(float db)
{
    maxBoostDb = std::clamp(db, 0.0f, maxBoostCeilingDb);
}

void PredictiveAGC::setVadThresholdDecibels(float db)
{
    vadThresholdDb = std::clamp(db, -80.0f, 0.0f);
}

void PredictiveAGC::setDuck(float amount)
{
    duck = std::clamp(amount, 0.0f, 1.0f);
}

void PredictiveAGC::setMaxCutDecibels(float db) { maxCutDb = std::max(0.0f, db); }

void PredictiveAGC::setSpeed(float normalised)
{
    speed = std::clamp(normalised, 0.0f, 1.0f);
    if (sampleRate > 0.0)
        updateCoefficients();
}

void PredictiveAGC::setLinked(bool shouldLink) { linked = shouldLink; }

void PredictiveAGC::setBypassed(bool shouldBypass) { bypassed = shouldBypass; }

float PredictiveAGC::getGainDecibels() const
{
    return static_cast<float>(channels[0].gainDb);
}

float PredictiveAGC::getEngageProbability() const
{
    return channels[0].probability;
}

bool PredictiveAGC::isEngaged() const
{
    return channels[0].latched;
}

int PredictiveAGC::getLatencySamples() const
{
    return latency;
}

void PredictiveAGC::updateDetector(Channel& channel, float magnitude)
{
    const double mag = static_cast<double>(std::max(0.0f, magnitude));
    const double vadLinear = std::pow(10.0, static_cast<double>(vadThresholdDb) / 20.0);
    // Hold across a waveform cycle so a quiet voice is not marked silent at
    // every zero crossing. A real pause still closes within about 12 ms.
    if (mag >= vadLinear)
        channel.vadHoldSamples = vadHoldBudget;
    else if (channel.vadHoldSamples > 0)
        --channel.vadHoldSamples;
    channel.vadOpen = channel.vadHoldSamples > 0;

    // A pause below the noise floor is not speech. Freeze the envelope so the
    // rider does not see the room and ask for more gain.
    if (!channel.vadOpen)
    {
        channel.slope += slopeCoef * (0.0 - channel.slope);
        channel.acceleration += accelCoef * (0.0 - channel.acceleration);
        channel.previousSlope = channel.slope;
        channel.latched = false;
        channel.probability = 0.0f;
        ++channel.sampleCount;
        return;
    }

    if (mag > channel.peak)
        channel.peak = mag;
    else
        channel.peak += peakReleaseCoef * (mag - channel.peak);

    channel.meanSquare += rmsCoef * (mag * mag - channel.meanSquare);

    const double peakDb = 20.0 * std::log10(std::max(channel.peak, 1.0e-8));
    const double rmsDb = 10.0 * std::log10(std::max(channel.meanSquare, 1.0e-16));
    const double levelDb = peakDb;
    const double dtMs = 1000.0 / sampleRate;

    const double rawSlope = (levelDb - channel.previousLevelDb) / dtMs;
    channel.slope += slopeCoef * (rawSlope - channel.slope);
    const double rawAccel = (channel.slope - channel.previousSlope) / dtMs;
    channel.acceleration += accelCoef * (rawAccel - channel.acceleration);
    channel.previousLevelDb = levelDb;
    channel.previousSlope = channel.slope;
    channel.levelDb = levelDb;

    if ((channel.sampleCount % inferInterval) == 0)
    {
        const float features[6] = {
            clampedFeature(levelDb - static_cast<double>(targetDb), 24.0),
            clampedFeature(channel.slope, 20.0),
            clampedFeature(channel.acceleration, 50.0),
            clampedFeature(peakDb - rmsDb, 12.0),
            static_cast<float>(std::clamp((levelDb - gateDb) / 20.0, 0.0, 1.0)),
            clampedFeature(levelDb, 40.0)
        };
        channel.probability = modelProbability(features);

        if (!channel.latched && channel.probability > engageOn)
            channel.latched = true;
        else if (channel.latched && channel.probability < engageOff)
            channel.latched = false;
    }

    if (levelDb < gateDb)
        channel.latched = false;

    ++channel.sampleCount;
}

double PredictiveAGC::desiredGain(double levelDb) const
{
    const double over = levelDb - static_cast<double>(targetDb);
    if (over > 0.0)
    {
        // Wide knee, then only `duck` of the overshoot. 40% of a loud step is
        // a few dB, not a clamp onto the target.
        constexpr double duckKnee = 12.0;
        const double shaped = over < duckKnee ? (over * over / duckKnee) : over;
        return std::max(-shaped * static_cast<double>(duck), -static_cast<double>(maxCutDb));
    }

    const double need = -over;
    const double shaped = need < kneeDb ? need * (need / kneeDb) : need;
    return std::min(shaped, static_cast<double>(maxBoostDb));
}

void PredictiveAGC::updateGain(Channel& channel)
{
    double desired = channel.gainDb;
    double coef = 0.0;

    if (bypassed)
    {
        desired = 0.0;
        coef = bypassCoef;
    }
    else if (!channel.vadOpen)
    {
        // alpha_hold = 1. The release filter does not move, so a pause cannot
        // boost the room or breathe back toward unity.
        channel.latched = false;
        channel.hangLeft = 0;
        channel.releaseArmed = true;
        coef = 0.0;
    }
    else if (!channel.latched)
    {
        channel.hangLeft = 0;
        channel.releaseArmed = true;
        coef = 0.0;
    }
    else
    {
        desired = desiredGain(channel.levelDb);

        if (desired < channel.gainDb - 0.05)
        {
            channel.hangLeft = 0;
            channel.releaseArmed = true;
            coef = attackCoef;
        }
        else
        {
            if (channel.releaseArmed)
            {
                channel.hangLeft = hangSamples;
                channel.releaseArmed = false;
            }

            if (channel.hangLeft > 0)
            {
                --channel.hangLeft;
                coef = 0.0;
            }
            else
            {
                coef = releaseCoef;
            }
        }
    }

    channel.gainDb += coef * (desired - channel.gainDb);
}

float PredictiveAGC::readDelay(Channel& channel, float input)
{
    if (latency <= 0)
        return input;

    const float delayed = channel.delayStorage[channel.writeIndex];
    channel.delayStorage[channel.writeIndex] = input;
    channel.writeIndex = (channel.writeIndex + 1) % latency;
    return delayed;
}

void PredictiveAGC::process(float* const* channelData, int numChannels, int numSamples)
{
    if (channelData == nullptr || numChannels <= 0 || numSamples <= 0 || preparedChannels <= 0)
        return;

    const int count = std::min(numChannels, preparedChannels);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (linked)
        {
            float detector = 0.0f;
            for (int ch = 0; ch < count; ++ch)
            {
                if (channelData[ch] == nullptr)
                    continue;
                detector = std::max(detector, std::abs(channelData[ch][sample]));
            }

            updateDetector(channels[0], detector);
            updateGain(channels[0]);
            const float gain = std::exp(static_cast<float>(channels[0].gainDb) * 0.11512925465f);

            for (int ch = 0; ch < count; ++ch)
            {
                if (channelData[ch] == nullptr)
                    continue;
                const float delayed = readDelay(channels[ch], channelData[ch][sample]);
                channelData[ch][sample] = delayed * gain;
            }
        }
        else
        {
            for (int ch = 0; ch < count; ++ch)
            {
                if (channelData[ch] == nullptr)
                    continue;

                const float input = channelData[ch][sample];
                updateDetector(channels[ch], std::abs(input));
                updateGain(channels[ch]);
                const float gain = std::exp(static_cast<float>(channels[ch].gainDb) * 0.11512925465f);
                channelData[ch][sample] = readDelay(channels[ch], input) * gain;
            }
        }
    }
}

} // namespace dsp
