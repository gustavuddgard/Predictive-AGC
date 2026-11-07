#!/usr/bin/env python3
"""Train the AGC 2.0 engage predictor and write Source/EngageWeights.h.

The detector math here is the source of truth for Source/PredictiveAGC.cpp.
Labels follow the level over the next 1.5 ms: engage when a sample in that
window sits more than 2 dB off the target and above the noise gate. Samples
below the gate are not engagement targets.

A logistic model is used when it clears the held-out bar and the listening
checks. Otherwise the same script fits one hidden layer of four tanh units.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np

SR = 48000
TARGET_DB = -18.0
HORIZON = int(round(0.0015 * SR))  # 72 samples, 1.5 ms
GATE_DB = -60.0
OFF_TARGET_DB = 2.0

PEAK_RELEASE_SEC = 0.020
RMS_SEC = 0.005
SLOPE_SMOOTH_SEC = 0.0005
ACCEL_SMOOTH_SEC = 0.0005

ERROR_SCALE = 24.0
SLOPE_SCALE = 20.0
ACCEL_SCALE = 50.0
CREST_SCALE = 12.0
LEVEL_SCALE = 40.0
FEATURE_LIMIT = 3.0


def coef(seconds: float, sr: float = SR) -> float:
    return 1.0 - math.exp(-1.0 / (seconds * sr))


def db_to_amp(db: float) -> float:
    return 10.0 ** (db / 20.0)


def clamp(value: float, lo: float, hi: float) -> float:
    return lo if value < lo else hi if value > hi else value


def features_from_signal(samples: np.ndarray, target_db: float = TARGET_DB) -> tuple[np.ndarray, np.ndarray]:
    """Return (features[N, 6], level_db[N]) using the runtime detector."""
    peak_coef = coef(PEAK_RELEASE_SEC)
    rms_coef = coef(RMS_SEC)
    slope_coef = coef(SLOPE_SMOOTH_SEC)
    accel_coef = coef(ACCEL_SMOOTH_SEC)
    dt_ms = 1000.0 / SR

    peak = 0.0
    mean_sq = 0.0
    prev_level = -160.0
    slope = 0.0
    prev_slope = 0.0
    accel = 0.0

    feats = np.zeros((len(samples), 6), dtype=np.float64)
    level = np.zeros(len(samples), dtype=np.float64)

    for i, sample in enumerate(samples):
        mag = abs(float(sample))
        if mag > peak:
            peak = mag
        else:
            peak += peak_coef * (mag - peak)

        mean_sq += rms_coef * (mag * mag - mean_sq)
        peak_db = 20.0 * math.log10(max(peak, 1e-8))
        rms_db = 10.0 * math.log10(max(mean_sq, 1e-16))
        level_db = peak_db

        raw_slope = (level_db - prev_level) / dt_ms
        slope += slope_coef * (raw_slope - slope)
        raw_accel = (slope - prev_slope) / dt_ms
        accel += accel_coef * (raw_accel - accel)
        prev_level = level_db
        prev_slope = slope

        crest = peak_db - rms_db
        activity = clamp((level_db - GATE_DB) / 20.0, 0.0, 1.0)

        feats[i, 0] = clamp((level_db - target_db) / ERROR_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT)
        feats[i, 1] = clamp(slope / SLOPE_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT)
        feats[i, 2] = clamp(accel / ACCEL_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT)
        feats[i, 3] = clamp(crest / CREST_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT)
        feats[i, 4] = activity
        feats[i, 5] = clamp(level_db / LEVEL_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT)
        level[i] = level_db

    return feats, level


def labels_from_level(level_db: np.ndarray) -> np.ndarray:
    labels = np.zeros(len(level_db), dtype=np.float64)
    horizon = HORIZON
    for i in range(len(level_db)):
        end = min(len(level_db), i + horizon)
        engage = 0.0
        for k in range(i, end):
            value = level_db[k]
            if value < GATE_DB:
                continue
            if abs(value - TARGET_DB) > OFF_TARGET_DB:
                engage = 1.0
                break
        labels[i] = engage
    return labels


def sine(db: float, n: int, freq: float = 1000.0, phase: float = math.pi / 2) -> np.ndarray:
    t = np.arange(n) / SR
    return db_to_amp(db) * np.sin(2.0 * math.pi * freq * t + phase)


def clip_examples(samples: np.ndarray, skip: int = int(0.03 * SR)) -> tuple[np.ndarray, np.ndarray]:
    feats, level = features_from_signal(samples)
    labels = labels_from_level(level)
    if len(samples) <= skip + 10:
        return feats, labels
    return feats[skip:], labels[skip:]


def build_dataset() -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    rng = np.random.default_rng(7)
    chunks_x: list[np.ndarray] = []
    chunks_y: list[np.ndarray] = []
    hold_x: list[np.ndarray] = []
    hold_y: list[np.ndarray] = []

    def add(samples: np.ndarray, holdout: bool = False, skip: int = int(0.03 * SR)) -> None:
        x, y = clip_examples(samples, skip=skip)
        (hold_x if holdout else chunks_x).append(x)
        (hold_y if holdout else chunks_y).append(y)

    n = int(0.25 * SR)
    add(np.zeros(int(0.4 * SR)), skip=int(0.01 * SR))
    add(sine(-18.0, n))
    add(sine(-17.0, n))
    add(sine(-19.0, n))
    add(sine(-16.5, n))
    add(sine(-20.0, n, freq=220.0))

    for db in (-6.0, -10.0, -12.0, -14.0, -24.0, -30.0, -36.0, -45.0, -54.0):
        add(sine(db, n, freq=880.0))

    # Steady levels in 2 dB steps. These define the engage boundary.
    for db in np.arange(-72.0, 1.0, 2.0):
        add(sine(float(db), int(0.08 * SR), freq=500.0), skip=int(0.02 * SR))

    def step_clip(a: float, b: float, hold: float = 0.12) -> np.ndarray:
        n_hold = int(hold * SR)
        env = np.concatenate([
            np.full(n_hold, db_to_amp(a)),
            np.full(n_hold, db_to_amp(b)),
        ])
        t = np.arange(len(env)) / SR
        return env * np.sin(2.0 * math.pi * 1000.0 * t + math.pi / 2)

    add(step_clip(-18.0, -6.0), skip=int(0.02 * SR))
    add(step_clip(-18.0, -30.0), skip=int(0.02 * SR))
    add(step_clip(-30.0, -18.0), skip=int(0.02 * SR))
    add(step_clip(-12.0, -18.0), skip=int(0.02 * SR))
    add(step_clip(-18.0, -8.0), holdout=True, skip=int(0.02 * SR))

    # Ramp across the target in decibels.
    ramp_n = int(0.06 * SR)
    ramp_db = np.linspace(-30.0, -6.0, ramp_n)
    t = np.arange(ramp_n) / SR
    add(db_to_amp(0.0) * (10.0 ** (ramp_db / 20.0)) * np.sin(2.0 * math.pi * 440.0 * t + math.pi / 2),
        skip=int(0.005 * SR))

    noise = rng.normal(0.0, db_to_amp(-14.0), int(0.2 * SR))
    burst = np.concatenate([np.zeros(int(0.05 * SR)), noise, np.zeros(int(0.08 * SR))])
    add(burst, skip=int(0.01 * SR))

    tone = sine(-10.0, int(0.3 * SR), freq=660.0)
    tone[int(0.12 * SR):int(0.18 * SR)] = 0.0
    add(tone, skip=int(0.02 * SR))

    # Held-out steady tones at the two operating points the plugin test uses.
    add(sine(-18.0, int(0.2 * SR), freq=1000.0), holdout=True)
    add(sine(-6.0, int(0.2 * SR), freq=1000.0), holdout=True)
    add(np.zeros(int(0.2 * SR)), holdout=True, skip=int(0.01 * SR))

    x = np.concatenate(chunks_x)
    y = np.concatenate(chunks_y)
    hx = np.concatenate(hold_x)
    hy = np.concatenate(hold_y)
    return x, y, hx, hy


def sigmoid(z: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-np.clip(z, -20.0, 20.0)))


def accuracy(p: np.ndarray, y: np.ndarray) -> float:
    return float(np.mean((p >= 0.5) == (y >= 0.5)))


def fit_logistic(x: np.ndarray, y: np.ndarray, epochs: int = 500, lr: float = 0.4):
    weights = np.zeros(x.shape[1], dtype=np.float64)
    bias = 0.0
    pos = max(float(y.sum()), 1.0)
    neg = max(float(len(y) - pos), 1.0)
    sample_w = np.where(y >= 0.5, neg / pos, pos / neg)
    sample_w /= sample_w.mean()

    for _ in range(epochs):
        p = sigmoid(x @ weights + bias)
        dz = sample_w * (p - y)
        weights -= lr * ((x.T @ dz) / len(y) + 1e-4 * weights)
        bias -= lr * float(dz.mean())
    return weights, bias


def fit_mlp(x: np.ndarray, y: np.ndarray, epochs: int = 700, lr: float = 0.15):
    rng = np.random.default_rng(11)
    hidden = 4
    w1 = rng.normal(0.0, 0.35, size=(hidden, x.shape[1]))
    b1 = np.zeros(hidden)
    w2 = rng.normal(0.0, 0.35, size=(hidden,))
    b2 = 0.0
    # Seed two units so the U-shaped error boundary is reachable immediately.
    w1[0, 0] = 2.0
    w1[1, 0] = -2.0
    w2[0] = 1.5
    w2[1] = 1.5

    pos = max(float(y.sum()), 1.0)
    neg = max(float(len(y) - pos), 1.0)
    sample_w = np.where(y >= 0.5, neg / pos, pos / neg)
    sample_w /= sample_w.mean()

    for epoch in range(epochs):
        hidden_pre = x @ w1.T + b1
        hidden_act = np.tanh(hidden_pre)
        z = hidden_act @ w2 + b2
        p = sigmoid(z)
        dz = sample_w * (p - y) / len(y)
        dw2 = hidden_act.T @ dz + 1e-4 * w2
        db2 = float(dz.sum())
        dh = dz[:, None] * w2
        dh_pre = dh * (1.0 - hidden_act * hidden_act)
        dw1 = dh_pre.T @ x + 1e-4 * w1
        db1 = dh_pre.sum(axis=0)
        step = lr * (0.6 if epoch > epochs * 0.7 else 1.0)
        w1 -= step * dw1
        b1 -= step * db1
        w2 -= step * dw2
        b2 -= step * db2
    return w1, b1, w2, b2


def predict_logistic(x: np.ndarray, weights: np.ndarray, bias: float) -> np.ndarray:
    return sigmoid(x @ weights + bias)


def predict_mlp(x: np.ndarray, w1: np.ndarray, b1: np.ndarray, w2: np.ndarray, b2: float) -> np.ndarray:
    hidden_act = np.tanh(x @ w1.T + b1)
    return sigmoid(hidden_act @ w2 + b2)


def region_probability(samples: np.ndarray, predict, skip: int) -> float:
    feats, _ = features_from_signal(samples)
    window = feats[skip:]
    if len(window) == 0:
        return float(predict(feats).mean())
    return float(predict(window).mean())


def listening_checks(predict) -> dict[str, float]:
    steady = sine(-18.0, int(0.2 * SR))
    hot = sine(-6.0, int(0.2 * SR))
    quiet = sine(-36.0, int(0.2 * SR))
    silence = np.zeros(int(0.2 * SR))

    n_hold = int(0.12 * SR)
    step_n = n_hold * 2
    env = np.concatenate([np.full(n_hold, db_to_amp(-18.0)), np.full(n_hold, db_to_amp(-6.0))])
    t = np.arange(step_n) / SR
    step = env * np.sin(2.0 * math.pi * 1000.0 * t + math.pi / 2)
    step_feats, _ = features_from_signal(step)
    # The step lands on a tick of the 8-sample grid (n_hold is divisible by 8).
    after = predict(step_feats[n_hold:n_hold + HORIZON])

    return {
        "steady": region_probability(steady, predict, int(0.05 * SR)),
        "hot": region_probability(hot, predict, int(0.05 * SR)),
        "quiet": region_probability(quiet, predict, int(0.05 * SR)),
        "silence": region_probability(silence, predict, int(0.05 * SR)),
        "step": float(after.max()) if len(after) else 0.0,
        "step_first": float(after[0]) if len(after) else 0.0,
    }


def checks_pass(scores: dict[str, float]) -> bool:
    return (
        scores["steady"] < 0.35
        and scores["hot"] > 0.70
        and scores["quiet"] > 0.60
        and scores["silence"] < 0.45
        and scores["step"] > 0.65
    )


def fmt(values: np.ndarray) -> str:
    return ", ".join(f"{float(v):.8f}f" for v in np.asarray(values).reshape(-1))


def write_header(kind: str, payload: dict, path: Path) -> None:
    if kind == "logistic":
        kind_expr = "EngageModelKind::logistic"
        logistic_w = fmt(payload["w"])
        logistic_b = f"{float(payload['b']):.8f}f"
        w1 = ", ".join(["0.0f"] * 24)
        b1 = ", ".join(["0.0f"] * 4)
        w2 = ", ".join(["0.0f"] * 4)
        b2 = "0.0f"
    else:
        kind_expr = "EngageModelKind::mlp"
        logistic_w = ", ".join(["0.0f"] * 6)
        logistic_b = "0.0f"
        w1 = fmt(payload["w1"])
        b1 = fmt(payload["b1"])
        w2 = fmt(payload["w2"])
        b2 = f"{float(payload['b2']):.8f}f"

    text = f"""#pragma once

namespace dsp
{{

enum class EngageModelKind
{{
    logistic = 0,
    mlp = 1
}};

// Baked by tools/train_engage.py. Seven floats for the logistic model, or 33
// for the four-unit hidden layer. Inference is a dot product, not a runtime.
struct EngageWeights
{{
    static constexpr EngageModelKind kind = {kind_expr};
    static constexpr int inputCount = 6;
    static constexpr int hiddenCount = 4;

    static constexpr float logisticW[inputCount] = {{ {logistic_w} }};
    static constexpr float logisticB = {logistic_b};

    static constexpr float mlpW1[hiddenCount * inputCount] = {{ {w1} }};
    static constexpr float mlpB1[hiddenCount] = {{ {b1} }};
    static constexpr float mlpW2[hiddenCount] = {{ {w2} }};
    static constexpr float mlpB2 = {b2};
}};

}} // namespace dsp
"""
    path.write_text(text)


def main() -> None:
    x, y, hx, hy = build_dataset()
    print(f"train frames {len(y)} positive {y.mean():.3f} holdout {len(hy)} positive {hy.mean():.3f}")

    weights, bias = fit_logistic(x, y)
    logistic_hold = accuracy(predict_logistic(hx, weights, bias), hy)
    logistic_scores = listening_checks(lambda z: predict_logistic(z, weights, bias))
    print(f"logistic holdout {logistic_hold:.3f} scores {logistic_scores}")

    chosen = "logistic"
    payload = {"w": weights, "b": bias}
    if logistic_hold < 0.90 or not checks_pass(logistic_scores):
        w1, b1, w2, b2 = fit_mlp(x, y)
        mlp_hold = accuracy(predict_mlp(hx, w1, b1, w2, b2), hy)
        mlp_scores = listening_checks(lambda z: predict_mlp(z, w1, b1, w2, b2))
        print(f"mlp holdout {mlp_hold:.3f} scores {mlp_scores}")
        if mlp_hold < 0.90 or not checks_pass(mlp_scores):
            raise SystemExit("engage model did not clear the held-out bar or the listening checks")
        chosen = "mlp"
        payload = {"w1": w1, "b1": b1, "w2": w2, "b2": b2}

    root = Path(__file__).resolve().parents[1]
    header = root / "Source" / "EngageWeights.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    write_header(chosen, payload, header)
    print(f"wrote {header} kind {chosen}")


if __name__ == "__main__":
    main()
