// SPDX-License-Identifier: MIT AND LGPL-2.1-or-later
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.
// Adapted for LAML by Leeway Open Software, 2026; see PROVENANCE.md and keyspline.h.
#include "keyspline.h"

#include <cmath>

namespace laml::anim {

// NOTE on math (from upstream): most of the theory was gathered from
//   Eric W. Weisstein. "Bernstein Polynomial." From MathWorld--A Wolfram Web Resource.
//       B(0,3) = (1-t)^3
//       B(1,3) = 3(1-t)^2 t
//       B(2,3) = 3(1-t) t^2
//       B(3,3) = t^3

float KeySpline::ComputeSplineValueY(float t) const
{
    float oneMinusT = 1.0f - t;
    float oneMinusTSquared = oneMinusT * oneMinusT;
    float tSquared = t * t;
    float tCubed = t * tSquared;

    float b13 = 3.0f * oneMinusTSquared * t;
    float b23 = 3.0f * oneMinusT * tSquared;
    float b33 = tCubed;

    // P0 = (0,0) and P3 = (1,1), so this is simplified
    return b13 * y1_ + b23 * y2_ + b33;
}

void KeySpline::GetXAndDeltaX(float t, float *x, float *deltaX) const
{
    float oneMinusT = 1.0f - t;
    float oneMinusTSquared = oneMinusT * oneMinusT;
    float tSquared = t * t;
    float tCubed = t * tSquared;

    float b13 = 3.0f * oneMinusTSquared * t;
    float b23 = 3.0f * oneMinusT * tSquared;
    float b33 = tCubed;

    *x = b13 * x1_ + b23 * x2_ + b33;

    // dX/dt
    float bx = 3.0f * x1_;
    float cx = 3.0f * x2_;
    float bxcx = 2.0f * (cx - bx);
    float threeCx = 3.0f - cx;

    *deltaX = oneMinusTSquared * bx + t * oneMinusT * bxcx + threeCx * tSquared;
}

float KeySpline::GetSplineProgress(float linearProgress)
{
    if (linearProgress == 0 || linearProgress == 1)
        return linearProgress;

    // Dynamic search interval to clamp with
    float bottom = 0.0f;
    float top = 1.0f;
    float fuzz = 0.000001f;      // computational zero
    float accuracy = 0.001f;     // 1/3 the desired accuracy in X

    // Loop while improving the guess (pseudo-Newton method, ported as is from WPF upstream). The
    // iteration count is bounded here: the search interval halves or the guess is clamped inside it.
    float t = lastT_;
    for (unsigned guard = 0; top - bottom > fuzz && guard < 200; guard++)
    {
        float x, deltaX;
        GetXAndDeltaX(t, &x, &deltaX);
        float absDeltaX = std::fabs(deltaX);

        // Clamp down the search interval, relying on the monotonicity of X(t)
        if (x > linearProgress)
            top = t;
        else
            bottom = t;

        // The desired accuracy is ultimately in y, not in x, so it is multiplied by dx/dy = (dx/dt) / (dy/dt).
        // But dy/dt <= 3, so we omit that.
        if (std::fabs(x - linearProgress) < accuracy * absDeltaX)
            break;

        if (absDeltaX > fuzz)
        {
            // Nonzero derivative, use Newton-Raphson to obtain the next guess
            float next = t - (x - linearProgress) / deltaX;

            if (next >= top)
                t = (t + top) / 2;
            else if (next <= bottom)
                t = (t + bottom) / 2;
            else
                t = next;
        }
        else
        {
            t = (bottom + top) / 2.0f;   // zero derivative, halve the search interval
        }
    }

    lastT_ = t;
    return ComputeSplineValueY(t);
}

}  // namespace laml::anim
