#include "PredictiveAGC.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL " << message << "\n";
        ++failures;
    }
}

float ampFromDb(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

float peakDb(const std::vector<float>& buffer, int start, int end)
{
    float peak = 0.0f;
    for (int i = start; i < end; ++i)
        peak = std::max(peak, std::abs(buffer[static_cast<size_t>(i)]));
    if (peak < 1.0e-8f)
        return -160.0f;
    return 20.0f * std::log10(peak);
}

std::vector<float> tone(double sampleRate, float db, int samples, int startSample = 0)
{
    std::vector<float> buffer(static_cast<size_t>(samples), 0.0f);
    const float amp = ampFromDb(db);
    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double>(startSample + i) / sampleRate;
        buffer[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t + M_PI / 2.0));
    }
    return buffer;
}

void render(dsp::PredictiveAGC& agc, std::vector<float>& buffer, int block)
{
    const int total = static_cast<int>(buffer.size());
    for (int offset = 0; offset < total;)
    {
        const int count = std::min(block, total - offset);
        float* pointer = buffer.data() + offset;
        float* channels[] = { pointer };
        agc.process(channels, 1, count);
        offset += count;
    }
}

void testLatency()
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    for (double rate : rates)
    {
        dsp::PredictiveAGC agc;
        agc.prepare(rate, 1);
        const int samples = agc.getLatencySamples();
        const double ms = 1000.0 * static_cast<double>(samples) / rate;
        check(ms < 2.0, "latency is under 2 ms at " + std::to_string(rate) + " (" + std::to_string(ms) + " ms)");
        check(std::abs(ms - 1.5) < 0.03, "latency is 1.5 ms at " + std::to_string(rate) + " (" + std::to_string(ms) + " ms)");
        check(samples == dsp::PredictiveAGC::latencySamplesFor(rate), "reported latency matches the 1.5 ms rounding");
    }
}

void testSteadyToneIsLeftAlone()
{
    constexpr double rate = 48000.0;
    dsp::PredictiveAGC agc;
    agc.prepare(rate, 1);
    const int latency = agc.getLatencySamples();
    const int total = static_cast<int>(0.25 * rate);
    const auto dry = tone(rate, -18.0f, total);
    auto wet = dry;
    render(agc, wet, 8);

    const int tail = static_cast<int>(0.10 * rate);
    bool engaged = false;
    float maxProbability = 0.0f;
    // Re-run the tail flags by processing a fresh instance up to the tail, then sampling.
    dsp::PredictiveAGC probe;
    probe.prepare(rate, 1);
    auto probeBuffer = dry;
    const int head = total - tail;
    {
        float* pointer = probeBuffer.data();
        float* channels[] = { pointer };
        probe.process(channels, 1, head);
    }
    for (int offset = head; offset < total; offset += 8)
    {
        const int count = std::min(8, total - offset);
        float* pointer = probeBuffer.data() + offset;
        float* channels[] = { pointer };
        probe.process(channels, 1, count);
        engaged = engaged || probe.isEngaged();
        maxProbability = std::max(maxProbability, probe.getEngageProbability());
    }

    check(!engaged, "steady tone near the target stays disengaged");
    check(maxProbability < 0.5f, "steady engage probability stays below 0.5, was " + std::to_string(maxProbability));

    float maxDiff = 0.0f;
    for (int i = head; i < total; ++i)
    {
        const float expected = dry[static_cast<size_t>(i - latency)];
        maxDiff = std::max(maxDiff, std::abs(wet[static_cast<size_t>(i)] - expected));
    }
    check(maxDiff < 1.0e-4f, "steady tone is left alone, max diff " + std::to_string(maxDiff));
}

void testStepIsCaughtInsideTheDelay()
{
    constexpr double rate = 48000.0;
    dsp::PredictiveAGC agc;
    agc.prepare(rate, 1);
    const int latency = agc.getLatencySamples();
    const int hold = static_cast<int>(0.12 * rate);
    const int total = hold * 2;
    std::vector<float> dry(static_cast<size_t>(total), 0.0f);
    for (int i = 0; i < total; ++i)
    {
        const float db = i < hold ? -18.0f : -6.0f;
        const double t = static_cast<double>(i) / rate;
        dry[static_cast<size_t>(i)] = ampFromDb(db) * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t + M_PI / 2.0));
    }

    auto wet = dry;
    float maxProbability = 0.0f;
    for (int offset = 0; offset < total; offset += 8)
    {
        const int count = std::min(8, total - offset);
        float* pointer = wet.data() + offset;
        float* channels[] = { pointer };
        agc.process(channels, 1, count);
        if (offset + count > hold && offset < hold + latency)
            maxProbability = std::max(maxProbability, agc.getEngageProbability());
    }

    check(maxProbability > 0.6f, "step raises engage probability, was " + std::to_string(maxProbability));

    const int window = 64;
    const float inPeak = peakDb(dry, hold, hold + window);
    const float outPeak = peakDb(wet, hold + latency, hold + latency + window);
    check(outPeak < inPeak - 6.0f,
          "loud samples leave the delay pulled down (in " + std::to_string(inPeak)
              + " dB, out " + std::to_string(outPeak) + " dB)");
}

void testPauseHoldsGain()
{
    constexpr double rate = 48000.0;
    dsp::PredictiveAGC agc;
    agc.prepare(rate, 1);

    const int hotSamples = static_cast<int>(0.15 * rate);
    auto hot = tone(rate, -6.0f, hotSamples);
    render(agc, hot, 64);
    const float held = agc.getGainDecibels();
    check(held < -6.0f, "hot signal engages a real cut, gain " + std::to_string(held));
    check(agc.isEngaged(), "hot signal is latched");

    const int silentSamples = static_cast<int>(rate);
    std::vector<float> silence(static_cast<size_t>(silentSamples), 0.0f);
    render(agc, silence, 128);

    check(!agc.isEngaged(), "a pause below the noise floor lets go of the latch");
    check(std::abs(agc.getGainDecibels() - held) < 0.25f,
          "pause holds the gain instead of breathing, held " + std::to_string(held)
              + " now " + std::to_string(agc.getGainDecibels()));
}

void testNoiseFloorIsNotBoosted()
{
    constexpr double rate = 48000.0;
    dsp::PredictiveAGC agc;
    agc.prepare(rate, 1);
    const int total = static_cast<int>(0.30 * rate);
    const auto dry = tone(rate, -50.0f, total);
    auto wet = dry;
    render(agc, wet, 64);

    check(std::abs(agc.getGainDecibels()) < 0.5f,
          "signal under the noise floor is not boosted, gain " + std::to_string(agc.getGainDecibels()));
    check(!agc.isEngaged(), "signal under the noise floor stays disengaged");

    const int latency = agc.getLatencySamples();
    float maxDiff = 0.0f;
    for (int i = latency + 1000; i < total; ++i)
        maxDiff = std::max(maxDiff, std::abs(wet[static_cast<size_t>(i)] - dry[static_cast<size_t>(i - latency)]));
    check(maxDiff < 1.0e-4f, "noise-floor signal is left alone, max diff " + std::to_string(maxDiff));
}

void testBoostIsCappedAtSixDb()
{
    constexpr double rate = 48000.0;
    dsp::PredictiveAGC agc;
    agc.prepare(rate, 1);
    agc.setMaxBoostDecibels(18.0f);

    const int total = static_cast<int>(2.0 * rate);
    auto buffer = tone(rate, -36.0f, total);
    render(agc, buffer, 128);

    const float gain = agc.getGainDecibels();
    check(gain <= 6.05f, "boost stays at or under +6 dB, was " + std::to_string(gain));
    check(gain > 5.0f, "a quiet voiced signal still gets the allowed boost, was " + std::to_string(gain));

    const float out = peakDb(buffer, total - static_cast<int>(0.2 * rate), total);
    check(out > -33.0f && out < -27.0f, "capped boost lands near -30 dB, was " + std::to_string(out));
}

void testLinkMovesBothChannels()
{
    constexpr double rate = 48000.0;
    dsp::PredictiveAGC agc;
    agc.prepare(rate, 2);
    agc.setLinked(true);

    const int total = static_cast<int>(0.20 * rate);
    auto left = tone(rate, -6.0f, total);
    auto right = tone(rate, -18.0f, total);
    const int block = 64;
    for (int offset = 0; offset < total; offset += block)
    {
        const int count = std::min(block, total - offset);
        float* channels[] = { left.data() + offset, right.data() + offset };
        agc.process(channels, 2, count);
    }

    const int start = static_cast<int>(0.12 * rate);
    const float leftDb = peakDb(left, start, total);
    const float rightDb = peakDb(right, start, total);
    check(leftDb > -21.0f && leftDb < -15.0f, "linked hot channel settles near the target, was " + std::to_string(leftDb));
    check(rightDb > -34.0f && rightDb < -26.0f, "link ducks the quiet channel too, was " + std::to_string(rightDb));
}

void testBlockSizeInvariance()
{
    constexpr double rate = 48000.0;
    const int hold = static_cast<int>(0.05 * rate);
    const int total = hold * 2;
    std::vector<float> dry(static_cast<size_t>(total), 0.0f);
    for (int i = 0; i < total; ++i)
    {
        const float db = i < hold ? -18.0f : -8.0f;
        const double t = static_cast<double>(i) / rate;
        dry[static_cast<size_t>(i)] = ampFromDb(db) * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * t + 0.3));
    }

    auto run = [&](int block)
    {
        dsp::PredictiveAGC agc;
        agc.prepare(rate, 1);
        auto buffer = dry;
        render(agc, buffer, block);
        return buffer;
    };

    const auto a = run(8);
    const auto b = run(512);
    const auto c = run(total);

    float maxDiff = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        maxDiff = std::max(maxDiff, std::abs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]));
        maxDiff = std::max(maxDiff, std::abs(a[static_cast<size_t>(i)] - c[static_cast<size_t>(i)]));
    }
    check(maxDiff < 1.0e-5f, "block size changes the result, max diff " + std::to_string(maxDiff));
}
} // namespace

int main()
{
    testLatency();
    testSteadyToneIsLeftAlone();
    testStepIsCaughtInsideTheDelay();
    testPauseHoldsGain();
    testNoiseFloorIsNotBoosted();
    testBoostIsCappedAtSixDb();
    testLinkMovesBothChannels();
    testBlockSizeInvariance();

    if (failures != 0)
    {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "agc_tests passed\n";
    return EXIT_SUCCESS;
}
