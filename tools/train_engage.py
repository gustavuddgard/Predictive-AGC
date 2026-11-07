#!/usr/bin/env python3
"""Train the AGC 2.0 engage predictor and write Source/EngageWeights.h.

The detector math here is the source of truth for Source/PredictiveAGC.cpp.
Thirteen features: six from the level detector, then lookahead, target, duck,
max boost, max cut, noise floor, and speed. Lookahead is drawn from 1.5 ms
to 40 ms. A cut is labeled only when that lookahead can hide the 15 ms attack.
Duck at 0 and boost at 0 keep the desired gain at 0 dB.
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np

SR = 48000
GATE_DB = -60.0
OFF_TARGET_DB = 2.0
INPUT_COUNT = 13

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

# Default knobs, matching the plugin.
DEFAULT = dict(target=-18.0, duck=0.4, boost=6.0, cut=24.0, vad=-42.0, speed=0.5)


def coef(seconds: float, sr: float = SR) -> float:
    return 1.0 - math.exp(-1.0 / (seconds * sr))


def db_to_amp(db: float) -> float:
    return 10.0 ** (db / 20.0)


def clamp(value: float, lo: float, hi: float) -> float:
    return lo if value < lo else hi if value > hi else value


def detect(samples: np.ndarray) -> dict[str, np.ndarray]:
    """Control-free detector trace. Feature 0 is applied later with the live target."""
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

    level = np.zeros(len(samples), dtype=np.float64)
    slope_a = np.zeros(len(samples), dtype=np.float64)
    accel_a = np.zeros(len(samples), dtype=np.float64)
    crest_a = np.zeros(len(samples), dtype=np.float64)

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

        level[i] = level_db
        slope_a[i] = slope
        accel_a[i] = accel
        crest_a[i] = peak_db - rms_db

    return {"level": level, "slope": slope_a, "accel": accel_a, "crest": crest_a}


def desired_gain(level: np.ndarray, target: float, duck: float, boost: float, cut: float) -> np.ndarray:
    over = level - target
    pos = over > 0.0
    shaped = np.where(over < 12.0, (over * over) / 12.0, over)
    cut_gain = np.maximum(-shaped * duck, -cut)
    need = -over
    shaped_boost = np.where(need < 6.0, need * (need / 6.0), need)
    boost_gain = np.minimum(shaped_boost, boost)
    return np.where(pos, cut_gain, boost_gain)


def shift_forward(values: np.ndarray, samples: int) -> np.ndarray:
    out = np.empty_like(values)
    if samples <= 0 or len(values) == 0:
        return values.copy()
    if samples >= len(values):
        out[:] = values[-1]
        return out
    out[:-samples] = values[samples:]
    out[-samples:] = values[-1]
    return out


def control_row(horizon_ms: float, target: float, duck: float, boost: float, cut: float, vad: float, speed: float) -> np.ndarray:
    return np.array(
        [
            clamp((horizon_ms - 1.5) / 38.5, 0.0, 1.0),
            clamp((target + 20.0) / 20.0, -FEATURE_LIMIT, FEATURE_LIMIT),
            clamp(duck, 0.0, 1.0),
            clamp(boost / 6.0, 0.0, 1.0),
            clamp(cut / 40.0, 0.0, 1.5),
            clamp((vad + 42.0) / 18.0, -FEATURE_LIMIT, FEATURE_LIMIT),
            clamp(speed, 0.0, 1.0),
        ],
        dtype=np.float64,
    )


def frame_features(trace: dict[str, np.ndarray], index: np.ndarray, target: float, controls: np.ndarray) -> np.ndarray:
    level = trace["level"][index]
    audio = np.column_stack(
        [
            np.clip(np.abs(level - target) / ERROR_SCALE, 0.0, FEATURE_LIMIT),
            np.clip(trace["slope"][index] / SLOPE_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT),
            np.clip(trace["accel"][index] / ACCEL_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT),
            np.clip(trace["crest"][index] / CREST_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT),
            np.clip((level - GATE_DB) / 20.0, 0.0, 1.0),
            np.clip(level / LEVEL_SCALE, -FEATURE_LIMIT, FEATURE_LIMIT),
        ]
    )
    tiled = np.repeat(controls.reshape(1, -1), len(index), axis=0)
    return np.concatenate([audio, tiled], axis=1)


def labels_for(level: np.ndarray, horizon_ms: float, target: float, duck: float, boost: float, cut: float, vad: float) -> np.ndarray:
    gain = desired_gain(level, target, duck, boost, cut)
    horizon = max(1, int(round(horizon_ms * 0.001 * SR)))
    end_level = shift_forward(level, horizon)
    end_gain = shift_forward(gain, horizon)
    voiced = level >= vad
    end_voiced = end_level >= vad
    sustained = (np.abs(level - target) > OFF_TARGET_DB) & (np.abs(end_level - target) > OFF_TARGET_DB) & voiced & end_voiced
    boost_move = (gain > 0.5) & (end_gain > 0.5) & voiced & end_voiced
    # A downward move only engages when the 15 ms attack fits inside the lookahead.
    track = sustained & (horizon_ms >= 15.0)
    return (boost_move | track).astype(np.float64)


def sine(db: float, n: int, freq: float = 1000.0, phase: float = math.pi / 2) -> np.ndarray:
    t = np.arange(n) / SR
    return db_to_amp(db) * np.sin(2.0 * math.pi * freq * t + phase)


def specs_for(rng: np.random.Generator) -> list[tuple[float, float, float, float, float, float, float]]:
    """Lookahead plus the six knobs. Endpoints and a transparent setting are always present."""
    specs = [
        (1.5, -18.0, 0.4, 6.0, 24.0, -42.0, 0.5),
        (40.0, -18.0, 0.4, 6.0, 24.0, -42.0, 0.5),
        (40.0, -18.0, 0.0, 0.0, 24.0, -42.0, 0.5),
        (1.5, -18.0, 0.0, 6.0, 24.0, -42.0, 0.5),
        (float(rng.uniform(1.5, 40.0)), float(rng.uniform(-40.0, 0.0)), float(rng.uniform(0.0, 1.0)),
         float(rng.uniform(0.0, 6.0)), float(rng.uniform(0.0, 40.0)), float(rng.uniform(-60.0, -24.0)),
         float(rng.uniform(0.0, 1.0))),
    ]
    return specs


def rows_for(samples: np.ndarray, rng: np.random.Generator, skip: int) -> tuple[np.ndarray, np.ndarray]:
    trace = detect(samples)
    index = np.arange(skip, len(samples), 8)
    if len(index) == 0:
        return np.zeros((0, INPUT_COUNT)), np.zeros((0,))
    chunks_x = []
    chunks_y = []
    for horizon, target, duck, boost, cut, vad, speed in specs_for(rng):
        controls = control_row(horizon, target, duck, boost, cut, vad, speed)
        chunks_x.append(frame_features(trace, index, target, controls))
        chunks_y.append(labels_for(trace["level"], horizon, target, duck, boost, cut, vad)[index])
    return np.concatenate(chunks_x), np.concatenate(chunks_y)


def build_dataset(rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    n = int(0.25 * SR)
    clips_train: list[tuple[np.ndarray, int]] = []
    clips_hold: list[tuple[np.ndarray, int]] = []

    def push(samples: np.ndarray, holdout: bool = False, skip: int = int(0.02 * SR)) -> None:
        (clips_hold if holdout else clips_train).append((samples, skip))

    push(np.zeros(int(0.4 * SR)), skip=int(0.01 * SR))
    push(sine(-18.0, n))
    push(sine(-17.0, n))
    push(sine(-19.0, n))
    push(sine(-16.5, n))
    push(sine(-20.0, n, freq=220.0))

    for db in (-6.0, -10.0, -12.0, -14.0, -24.0, -30.0, -36.0, -45.0, -54.0):
        push(sine(db, n, freq=880.0))

    for db in np.arange(-72.0, 1.0, 2.0):
        push(sine(float(db), int(0.08 * SR), freq=500.0), skip=int(0.02 * SR))

    def step_clip(a: float, b: float, hold_s: float = 0.12) -> np.ndarray:
        n_hold = int(hold_s * SR)
        env = np.concatenate([np.full(n_hold, db_to_amp(a)), np.full(n_hold, db_to_amp(b))])
        t = np.arange(len(env)) / SR
        return env * np.sin(2.0 * math.pi * 1000.0 * t + math.pi / 2)

    push(step_clip(-18.0, -6.0))
    push(step_clip(-18.0, -30.0))
    push(step_clip(-30.0, -18.0))
    push(step_clip(-12.0, -18.0))
    push(step_clip(-18.0, -8.0), holdout=True)

    ramp_n = int(0.06 * SR)
    ramp_db = np.linspace(-30.0, -6.0, ramp_n)
    t = np.arange(ramp_n) / SR
    push(db_to_amp(0.0) * (10.0 ** (ramp_db / 20.0)) * np.sin(2.0 * math.pi * 440.0 * t + math.pi / 2),
         skip=int(0.005 * SR))

    noise = rng.normal(0.0, db_to_amp(-14.0), int(0.2 * SR))
    burst = np.concatenate([np.zeros(int(0.05 * SR)), noise, np.zeros(int(0.08 * SR))])
    push(burst, skip=int(0.01 * SR))

    tone = sine(-10.0, int(0.3 * SR), freq=660.0)
    tone[int(0.12 * SR):int(0.18 * SR)] = 0.0
    push(tone)

    push(sine(-18.0, int(0.2 * SR), freq=1000.0), holdout=True)
    push(sine(-6.0, int(0.2 * SR), freq=1000.0), holdout=True)
    push(np.zeros(int(0.2 * SR)), holdout=True, skip=int(0.01 * SR))

    def expand(clips: list[tuple[np.ndarray, int]]) -> tuple[np.ndarray, np.ndarray]:
        xs = []
        ys = []
        for samples, skip in clips:
            x, y = rows_for(samples, rng, skip)
            if len(y):
                xs.append(x)
                ys.append(y)
        return np.concatenate(xs), np.concatenate(ys)

    x, y = expand(clips_train)
    hx, hy = expand(clips_hold)
    return x, y, hx, hy


def sigmoid(z: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-np.clip(z, -20.0, 20.0)))


def accuracy(p: np.ndarray, y: np.ndarray) -> float:
    return float(np.mean((p >= 0.5) == (y >= 0.5)))


def fit_logistic(x: np.ndarray, y: np.ndarray, constraints=(), epochs: int = 500, lr: float = 0.4):
    weights = np.zeros(x.shape[1], dtype=np.float64)
    bias = 0.0
    pos = max(float(y.sum()), 1.0)
    neg = max(float(len(y) - pos), 1.0)
    sample_w = np.where(y >= 0.5, neg / pos, pos / neg)
    quiet_engage = (y >= 0.5) & (x[:, 0] > 0.15) & (x[:, 5] < -0.7)
    sample_w = np.where(quiet_engage, sample_w * 3.0, sample_w)
    sample_w /= sample_w.mean()

    for _ in range(epochs):
        p = sigmoid(x @ weights + bias)
        dz = sample_w * (p - y)
        l2 = np.full(x.shape[1], 1.0e-4, dtype=np.float64)
        l2[3] = 1.0e-3
        weights -= lr * ((x.T @ dz) / len(y) + l2 * weights)
        bias -= lr * float(dz.mean())

        for row, bound, direction in constraints:
            z = float(row @ weights + bias)
            probability = float(sigmoid(np.array([z]))[0])
            violated = probability > bound if direction == "lt" else probability < bound
            if not violated:
                continue
            grad = 8.0 * (probability - bound) * probability * (1.0 - probability)
            weights -= lr * grad * row
            bias -= lr * grad
    return weights, bias


def predict_logistic(x: np.ndarray, weights: np.ndarray, bias: float) -> np.ndarray:
    return sigmoid(x @ weights + bias)


def scenario_features(samples: np.ndarray, horizon_ms: float, skip: int, **knobs) -> np.ndarray:
    spec = dict(DEFAULT)
    spec.update(knobs)
    trace = detect(samples)
    index = np.arange(skip, len(samples))
    if len(index) == 0:
        index = np.arange(len(samples))
    controls = control_row(horizon_ms, spec["target"], spec["duck"], spec["boost"], spec["cut"], spec["vad"], spec["speed"])
    return frame_features(trace, index, spec["target"], controls)


def mean_feature(samples: np.ndarray, horizon_ms: float, skip: int, **knobs) -> np.ndarray:
    return scenario_features(samples, horizon_ms, skip, **knobs).mean(axis=0)


def listening_checks(predict) -> dict[str, float]:
    skip = int(0.05 * SR)
    steady = sine(-18.0, int(0.2 * SR))
    hot = sine(-6.0, int(0.2 * SR))
    quiet = sine(-36.0, int(0.2 * SR))
    silence = np.zeros(int(0.2 * SR))

    n_hold = int(0.12 * SR)
    env = np.concatenate([np.full(n_hold, db_to_amp(-18.0)), np.full(n_hold, db_to_amp(-6.0))])
    t = np.arange(n_hold * 2) / SR
    step = env * np.sin(2.0 * math.pi * 1000.0 * t + math.pi / 2)
    step_trace = detect(step)

    def step_p(horizon_ms: float) -> float:
        controls = control_row(horizon_ms, -18.0, 0.4, 6.0, 24.0, -42.0, 0.5)
        feats = frame_features(step_trace, np.array([n_hold]), -18.0, controls)
        return float(predict(feats)[0])

    def step_max(horizon_ms: float) -> float:
        controls = control_row(horizon_ms, -18.0, 0.4, 6.0, 24.0, -42.0, 0.5)
        index = np.arange(n_hold, len(step), 8)
        feats = frame_features(step_trace, index, -18.0, controls)
        return float(predict(feats).max())

    def mean_p(samples: np.ndarray, horizon_ms: float, **knobs) -> float:
        return float(predict(scenario_features(samples, horizon_ms, skip, **knobs)).mean())

    return {
        "steady": mean_p(steady, 40.0),
        "hot40": mean_p(hot, 40.0),
        "hot15": mean_p(hot, 1.5),
        "quiet": mean_p(quiet, 1.5),
        "quiet_flat": mean_p(quiet, 1.5, boost=0.0, duck=0.0),
        "silence": mean_p(silence, 40.0),
        "step40": step_p(40.0),
        "step15": step_p(1.5),
        "step15_max": step_max(1.5),
        "clear40": mean_p(hot, 40.0, duck=0.0, boost=0.0),
    }


def checks_pass(scores: dict[str, float]) -> bool:
    return (
        scores["steady"] < 0.35
        and scores["hot40"] > 0.70
        and scores["hot15"] < 0.45
        and scores["quiet"] > 0.60
        and scores["quiet_flat"] < 0.45
        and scores["silence"] < 0.45
        and scores["step40"] > 0.65
        and scores["step15"] < 0.45
        and scores["step15_max"] < 0.50
        and scores["clear40"] > 0.55
    )


def fmt(values: np.ndarray) -> str:
    return ", ".join(f"{float(v):.8f}f" for v in np.asarray(values).reshape(-1))


def write_header(weights: np.ndarray, bias: float, path: Path) -> None:
    zeros_w1 = ", ".join(["0.0f"] * (4 * INPUT_COUNT))
    zeros_b = ", ".join(["0.0f"] * 4)
    text = f"""#pragma once

namespace dsp
{{

enum class EngageModelKind
{{
    logistic = 0,
    mlp = 1
}};

// Baked by tools/train_engage.py. Thirteen logistic weights: six detector
// features, then lookahead, target, duck, max boost, max cut, noise floor, speed.
struct EngageWeights
{{
    static constexpr EngageModelKind kind = EngageModelKind::logistic;
    static constexpr int inputCount = {INPUT_COUNT};
    static constexpr int hiddenCount = 4;

    static constexpr float logisticW[inputCount] = {{ {fmt(weights)} }};
    static constexpr float logisticB = {float(bias):.8f}f;

    static constexpr float mlpW1[hiddenCount * inputCount] = {{ {zeros_w1} }};
    static constexpr float mlpB1[hiddenCount] = {{ {zeros_b} }};
    static constexpr float mlpW2[hiddenCount] = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    static constexpr float mlpB2 = 0.0f;
}};

}} // namespace dsp
"""
    path.write_text(text)


def main() -> None:
    rng = np.random.default_rng(7)
    x, y, hx, hy = build_dataset(rng)
    print(f"train frames {len(y)} positive {y.mean():.3f} holdout {len(hy)} positive {hy.mean():.3f}")

    skip = int(0.05 * SR)
    n_hold = int(0.12 * SR)
    step_env = np.concatenate([np.full(n_hold, db_to_amp(-18.0)), np.full(n_hold, db_to_amp(-6.0))])
    step_t = np.arange(n_hold * 2) / SR
    step = step_env * np.sin(2.0 * math.pi * 1000.0 * step_t + math.pi / 2)
    step_trace = detect(step)

    def step_row(horizon_ms: float) -> np.ndarray:
        controls = control_row(horizon_ms, -18.0, 0.4, 6.0, 24.0, -42.0, 0.5)
        return frame_features(step_trace, np.array([n_hold]), -18.0, controls)[0]

    constraints = (
        (mean_feature(sine(-18.0, int(0.2 * SR)), 40.0, skip), 0.28, "lt"),
        (mean_feature(sine(-6.0, int(0.2 * SR)), 40.0, skip), 0.78, "gt"),
        (mean_feature(sine(-6.0, int(0.2 * SR)), 1.5, skip), 0.32, "lt"),
        (mean_feature(sine(-36.0, int(0.2 * SR)), 1.5, skip), 0.70, "gt"),
        (mean_feature(sine(-36.0, int(0.2 * SR)), 1.5, skip, boost=0.0, duck=0.0), 0.32, "lt"),
        (mean_feature(np.zeros(int(0.2 * SR)), 40.0, skip), 0.25, "lt"),
        (mean_feature(sine(-6.0, int(0.2 * SR)), 40.0, skip, duck=0.0, boost=0.0), 0.62, "gt"),
        (step_row(1.5), 0.30, "lt"),
        (step_row(1.5), 0.30, "lt"),
        (step_row(1.5), 0.30, "lt"),
        (step_row(40.0), 0.80, "gt"),
        (step_row(40.0), 0.80, "gt"),
    )
    crest_index = np.arange(n_hold, n_hold + int(0.002 * SR), 8)
    crest_controls = control_row(1.5, -18.0, 0.4, 6.0, 24.0, -42.0, 0.5)
    crest_rows = frame_features(step_trace, crest_index, -18.0, crest_controls)
    constraints = constraints + tuple((row, 0.40, "lt") for row in crest_rows)

    weights, bias = fit_logistic(x, y, constraints)
    hold = accuracy(predict_logistic(hx, weights, bias), hy)
    scores = listening_checks(lambda z: predict_logistic(z, weights, bias))
    print(f"logistic holdout {hold:.3f} scores {scores}")
    print("weights " + " ".join(f"{float(v):.4f}" for v in weights) + f" bias {bias:.4f}")

    if hold < 0.90 or not checks_pass(scores):
        raise SystemExit("logistic engage model did not clear the held-out bar or the listening checks")

    root = Path(__file__).resolve().parents[1]
    header = root / "Source" / "EngageWeights.h"
    write_header(weights, bias, header)
    print(f"wrote {header}")


if __name__ == "__main__":
    main()
