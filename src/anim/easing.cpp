// SPDX-License-Identifier: MIT AND LGPL-2.1-or-later
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.
// Adapted for LAML by Leeway Open Software, 2026; see PROVENANCE.md and easing.h.
#include "easing.h"

#include <cmath>

namespace laml::anim {

namespace {
constexpr float kPi = 3.1415926535897932384626433832795f;
constexpr float kE = 2.7182818284590452353602874713527f;
constexpr double kEpsilon = 0.0001;
}  // namespace

float Easing::Ease(float normalizedTime) const
{
    switch (mode)
    {
    case EasingMode::EaseIn:
        return EaseInCore(normalizedTime);
    case EasingMode::EaseOut:
        return 1.0f - EaseInCore(1.0f - normalizedTime);
    case EasingMode::EaseInOut:
        if (normalizedTime < 0.5f)
            return EaseInCore(normalizedTime * 2.0f) / 2.0f;
        return (1.0f - EaseInCore(2.0f - normalizedTime * 2.0f)) / 2.0f + 0.5f;
    }
    return 0;   // no other mode exists
}

float Easing::EaseInCore(float t) const
{
    switch (kind)
    {
    case EasingKind::Circle:
        if (t < -1.0f) t = -1.0f;
        if (t > 1.0f) t = 1.0f;
        return -1.0f * (sqrtf(1.0f - (t * t)) - 1.0f);

    case EasingKind::Back:
        if (t < 0.0f) t = 0.0f;
        return (t * t * t) - t * amplitude * sinf(t * kPi);

    case EasingKind::Exponential:
        if (exponent >= -kEpsilon && exponent <= kEpsilon)
            return t;
        return (powf(kE, exponent * t) - 1.0f) / (powf(kE, exponent) - 1.0f);

    case EasingKind::Power:
    {
        float effectivePower = power > 0.0f ? power : 0.0f;
        return powf(t, effectivePower);
    }

    case EasingKind::Quadratic:
        return t * t;
    case EasingKind::Cubic:
        return t * t * t;
    case EasingKind::Quartic:
        return t * t * t * t;
    case EasingKind::Quintic:
        return t * t * t * t * t;

    case EasingKind::Elastic:
    {
        float osc = static_cast<float>(oscillations >= 0 ? oscillations : 0);
        float exponentialModifier;
        if (springiness >= -kEpsilon && springiness <= kEpsilon)
            exponentialModifier = t;
        else
            exponentialModifier = (powf(kE, springiness * t) - 1.0f) / (powf(kE, springiness) - 1.0f);
        return exponentialModifier * sinf(t * ((2.0f * kPi * osc) + (kPi / 2.0f)));
    }

    case EasingKind::Bounce:
    {
        float nb = (bounces >= 0) ? static_cast<float>(bounces) : 0.0f;

        // Clamp the bounciness so we don't hit a divide by zero
        float b = (bounciness <= 1.0f + kEpsilon) ? 1.01f : bounciness;

        float pw = powf(b, nb);
        float oneMinusB = 1.0f - b;

        // 'unit' space: geometric series with only half the last sum
        float sumOfUnits = (1.0f - pw) / oneMinusB + pw / 2.0f;
        float unitAtT = t * sumOfUnits;

        // 'bounce' space
        float bounceAtT = static_cast<float>(std::log(static_cast<double>(-unitAtT * (1.0f - b) + 1.0f)) /
                                             std::log(static_cast<double>(b)));
        float start = floorf(bounceAtT);
        float end = start + 1.0f;

        // 'time' space
        float startTime = (1.0f - powf(b, start)) / (oneMinusB * sumOfUnits);
        float endTime = (1.0f - powf(b, end)) / (oneMinusB * sumOfUnits);

        // Curve fitting for the bounce
        float midTime = (startTime + endTime) / 2.0f;
        float timeRelativeToPeak = t - midTime;
        float radius = midTime - startTime;
        float amp = powf(1.0f / b, (nb - start));

        // A quadratic through (startTime, 0) and (endTime, 0) that peaks at amp
        return (-amp / (radius * radius)) * (timeRelativeToPeak - radius) * (timeRelativeToPeak + radius);
    }

    case EasingKind::Sine:
        return 1.0f - sinf((1.0f - t) * (kPi / 2.0f));
    }
    return t;
}

}  // namespace laml::anim
