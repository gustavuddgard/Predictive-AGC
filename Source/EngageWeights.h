#pragma once

namespace dsp
{

enum class EngageModelKind
{
    logistic = 0,
    mlp = 1
};

// Baked by tools/train_engage.py. Seven floats for the logistic model, or 33
// for the four-unit hidden layer. Inference is a dot product, not a runtime.
struct EngageWeights
{
    static constexpr EngageModelKind kind = EngageModelKind::logistic;
    static constexpr int inputCount = 6;
    static constexpr int hiddenCount = 4;

    static constexpr float logisticW[inputCount] = { 5.80620145f, 0.25862628f, 0.20847212f, 1.01459102f, 4.42525659f, 5.35710972f };
    static constexpr float logisticB = -3.25445544f;

    static constexpr float mlpW1[hiddenCount * inputCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float mlpB1[hiddenCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float mlpW2[hiddenCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float mlpB2 = 0.0f;
};

} // namespace dsp
