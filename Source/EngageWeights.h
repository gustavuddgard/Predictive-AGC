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
    static constexpr EngageModelKind kind = EngageModelKind::mlp;
    static constexpr int inputCount = 6;
    static constexpr int hiddenCount = 4;

    static constexpr float logisticW[inputCount] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr float logisticB = 0.0f;

    static constexpr float mlpW1[hiddenCount * inputCount] = { 4.00590808f, 0.46273063f, 0.41411423f, 0.07539008f, -0.33434086f, 1.13434411f, -3.71960879f, -0.03205328f, 0.25771200f, -0.10055985f, -0.33814292f, -0.69053717f, 0.37450804f, -0.04924764f, -0.13213266f, 0.40207854f, 0.81649907f, 0.05246269f, -0.57969036f, 0.23762318f, -0.30063985f, -0.94777139f, -1.49364467f, -0.35047610f };
    static constexpr float mlpB1[hiddenCount] = { -0.23061759f, -0.86309176f, 0.31065800f, -1.06013613f };
    static constexpr float mlpW2[hiddenCount] = { 3.54891576f, 3.27944260f, 1.11762991f, -2.53286691f };
    static constexpr float mlpB2 = -0.12382893f;
};

} // namespace dsp
