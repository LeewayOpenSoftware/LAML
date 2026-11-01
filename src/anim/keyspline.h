// SPDX-License-Identifier: MIT AND LGPL-2.1-or-later
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.
// Adapted for LAML by Leeway Open Software, 2026; see PROVENANCE.md.
//
// Adapted from dxaml/xcp/core/inc/KeySpline.h and dxaml/xcp/components/animation/KeySpline.cpp of
// microsoft/microsoft-ui-xaml (commit 8da148dcd8ed65ea8b8ae664fbe45c9cc82e372b). The numerical method
// is the one of the upstream, which in turn was ported from WPF. The dependency object, the XAML
// string parser and the Windows types were removed; the class is plain C++ with no OS dependency.
#pragma once

namespace laml::anim {

// A cubic Bezier with fixed end points (0,0) and (1,1), used to shape the progress of a keyframe.
// Control point coordinates must be in [0,1] (as in XAML).
class KeySpline
{
public:
    // Defaults of the XAML KeySpline: control point 1 = (0,0), control point 2 = (1,1) (the identity curve).
    KeySpline() = default;
    KeySpline(float x1, float y1, float x2, float y2) : x1_(x1), y1_(y1), x2_(x2), y2_(y2) {}

    static bool IsControlPointValid(float x, float y) { return x >= 0.0f && y >= 0.0f && x <= 1.0f && y <= 1.0f; }
    bool IsValid() const { return IsControlPointValid(x1_, y1_) && IsControlPointValid(x2_, y2_); }

    float X1() const { return x1_; }
    float Y1() const { return y1_; }
    float X2() const { return x2_; }
    float Y2() const { return y2_; }

    // Y of the curve for a linear progress value (X) in [0,1]. Values 0 and 1 map to themselves.
    // Not thread-safe: the last solution is cached to speed up the next call, as upstream does.
    float GetSplineProgress(float linearProgress);

private:
    float ComputeSplineValueY(float t) const;
    void GetXAndDeltaX(float t, float *x, float *deltaX) const;

    float x1_ = 0.0f, y1_ = 0.0f;
    float x2_ = 1.0f, y2_ = 1.0f;
    float lastT_ = 0.0f;
};

}  // namespace laml::anim
