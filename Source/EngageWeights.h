#pragma once

namespace dsp
{

enum class EngageModelKind
{
    logistic = 0,
    mlp = 1
};

// Baked by tools/train_engage.py. Thirteen logistic weights: six detector
// features, then lookahead, target, duck, max boost, max cut, noise floor, speed.
struct EngageWeights
{
    static constexpr EngageModelKind kind = EngageModelKind::logistic;
    static constexpr int inputCount = 13;
    static constexpr int hiddenCount = 4;

    static constexpr float logisticW[inputCount] = { 4.36251687f, -1.79120654f, 1.73229138f, 0.27271705f, 12.31189860f, 0.48171396f, 1.92711706f, -0.72356592f, -0.51524011f, 1.33500687f, -5.73735458f, -1.19437336f, -4.11243112f };
    static constexpr float logisticB = -10.37284358f;

    static constexpr float mlpW1[hiddenCount * inputCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float mlpB1[hiddenCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float mlpW2[hiddenCount] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float mlpB2 = 0.0f;
};

} // namespace dsp
