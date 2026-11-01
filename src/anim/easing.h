// SPDX-License-Identifier: MIT AND LGPL-2.1-or-later
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.
// Adapted for LAML by Leeway Open Software, 2026; see PROVENANCE.md.
//
// Adapted from dxaml/xcp/components/animation/EasingFunctions.cpp and dxaml/xcp/core/inc/EasingFunctions.h
// of microsoft/microsoft-ui-xaml (commit 8da148dcd8ed65ea8b8ae664fbe45c9cc82e372b): the eleven XAML
// easing curves and the three easing modes. The dependency objects, the composition (WUC) bridge and the
// Windows types were removed; this is plain C++ with no OS dependency.
#pragma once

#include <cstdint>

namespace laml::anim {

enum class EasingMode { EaseIn, EaseOut, EaseInOut };

enum class EasingKind
{
    Back, Bounce, Circle, Cubic, Elastic, Exponential, Power, Quadratic, Quartic, Quintic, Sine,
};

// One easing function. Parameter defaults are those of the XAML classes: BackEase.Amplitude 1,
// BounceEase.Bounces 3 / Bounciness 2, ElasticEase.Oscillations 3 / Springiness 3,
// ExponentialEase.Exponent 2, PowerEase.Power 2.
struct Easing
{
    EasingKind kind = EasingKind::Quadratic;
    EasingMode mode = EasingMode::EaseOut;   // the default EasingMode of XAML's EasingFunctionBase
    float amplitude = 1.0f;                  // Back
    int32_t bounces = 3;                     // Bounce
    float bounciness = 2.0f;                 // Bounce
    int32_t oscillations = 3;                // Elastic
    float springiness = 3.0f;                // Elastic
    float exponent = 2.0f;                   // Exponential
    float power = 2.0f;                      // Power

    // Maps normalized time (0..1) to normalized progress (0..1 for most curves; Back and Elastic overshoot).
    float Ease(float normalizedTime) const;

private:
    float EaseInCore(float t) const;
};

}  // namespace laml::anim
