// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of the animation math (KeySpline, easing functions). Expected values are analytic or come
// from an independent solver written here, not from the code under test.
#include <cmath>
#include <cstdio>

#include "../src/anim/easing.h"
#include "../src/anim/keyspline.h"

using namespace laml::anim;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        if (!(cond))                                                         \
        {                                                                    \
            g_failed++;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)
#define NEAR(a, b, tol)                                                                          \
    do                                                                                           \
    {                                                                                            \
        g_checks++;                                                                              \
        double va_ = (a), vb_ = (b);                                                             \
        if (!(std::fabs(va_ - vb_) <= (tol)))                                                    \
        {                                                                                        \
            g_failed++;                                                                          \
            std::printf("FAIL %s:%d: %s = %.6f, expected %.6f (+-%g)\n", __FILE__, __LINE__, #a, va_, vb_, (double)(tol)); \
        }                                                                                        \
    } while (0)

// Independent reference: solve X(t) = x by bisection in double precision, return Y(t).
static double RefSpline(double x1, double y1, double x2, double y2, double x)
{
    double lo = 0, hi = 1;
    for (int i = 0; i < 80; i++)
    {
        double t = (lo + hi) / 2, u = 1 - t;
        double xt = 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t;
        (xt < x ? lo : hi) = t;
    }
    double t = (lo + hi) / 2, u = 1 - t;
    return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
}

static void TestKeySpline()
{
    KeySpline identity;   // XAML defaults: (0,0) and (1,1)
    CHECK(identity.IsValid());
    for (int i = 0; i <= 100; i++)
        NEAR(identity.GetSplineProgress(i / 100.0f), i / 100.0f, 0.003);
    NEAR(identity.GetSplineProgress(0.0f), 0.0, 0.0);
    NEAR(identity.GetSplineProgress(1.0f), 1.0, 0.0);

    // CSS "ease" curve, a published reference point: cubic-bezier(0.25, 0.1, 0.25, 1) at x = 0.5 is 0.8024
    KeySpline ease(0.25f, 0.1f, 0.25f, 1.0f);
    NEAR(ease.GetSplineProgress(0.5f), 0.8024, 0.003);

    // against the independent solver, for several curves, ascending and shuffled evaluation order
    const float curves[][4] = {{0.25f, 0.1f, 0.25f, 1.0f}, {0.42f, 0.0f, 0.58f, 1.0f}, {0.0f, 0.0f, 0.58f, 1.0f},
                               {0.42f, 0.0f, 1.0f, 1.0f},  {0.1f, 0.9f, 0.9f, 0.1f},   {0.0f, 1.0f, 1.0f, 0.0f},
                               {1.0f, 0.0f, 0.0f, 1.0f},   {0.6f, 0.04f, 0.98f, 0.335f}};
    for (const auto &c : curves)
    {
        KeySpline k(c[0], c[1], c[2], c[3]);
        CHECK(k.IsValid());
        for (int i = 1; i < 200; i++)
            NEAR(k.GetSplineProgress(i / 200.0f), RefSpline(c[0], c[1], c[2], c[3], i / 200.0), 0.006);
        uint32_t seed = 7;
        for (int i = 0; i < 400; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            double x = ((seed >> 8) % 999 + 1) / 1000.0;
            NEAR(k.GetSplineProgress((float)x), RefSpline(c[0], c[1], c[2], c[3], x), 0.006);
        }
        // the result stays inside [0,1] and X=0 / X=1 are exact
        NEAR(k.GetSplineProgress(0.0f), 0.0, 0.0);
        NEAR(k.GetSplineProgress(1.0f), 1.0, 0.0);
    }

    // control points outside [0,1] are not valid XAML key splines
    CHECK(!KeySpline(-0.1f, 0.0f, 0.5f, 1.0f).IsValid());
    CHECK(!KeySpline(0.0f, 0.0f, 1.1f, 1.0f).IsValid());
    CHECK(!KeySpline(0.0f, 1.5f, 0.5f, 1.0f).IsValid());
    CHECK(KeySpline::IsControlPointValid(0.0f, 1.0f));

    // out-of-range progress values must terminate (the search is bounded)
    KeySpline k(0.25f, 0.1f, 0.25f, 1.0f);
    (void)k.GetSplineProgress(-5.0f);
    (void)k.GetSplineProgress(5.0f);
    (void)k.GetSplineProgress(NAN);
    CHECK(true);
}

static Easing Make(EasingKind kind, EasingMode mode)
{
    Easing e;
    e.kind = kind;
    e.mode = mode;
    return e;
}

static void TestEasing()
{
    const EasingKind kinds[] = {EasingKind::Back,     EasingKind::Bounce,    EasingKind::Circle,  EasingKind::Cubic,
                                EasingKind::Elastic,  EasingKind::Exponential, EasingKind::Power,  EasingKind::Quadratic,
                                EasingKind::Quartic,  EasingKind::Quintic,   EasingKind::Sine};
    const EasingMode modes[] = {EasingMode::EaseIn, EasingMode::EaseOut, EasingMode::EaseInOut};

    for (EasingKind kind : kinds)
        for (EasingMode mode : modes)
        {
            Easing e = Make(kind, mode);
            NEAR(e.Ease(0.0f), 0.0, 1e-4);
            NEAR(e.Ease(1.0f), 1.0, 1e-4);
        }

    // mode relations, for every curve: out(t) = 1 - in(1 - t); in-out is continuous at 0.5 and equals 0.5 there
    for (EasingKind kind : kinds)
    {
        Easing in = Make(kind, EasingMode::EaseIn), out = Make(kind, EasingMode::EaseOut), io = Make(kind, EasingMode::EaseInOut);
        for (int i = 1; i < 100; i++)
        {
            float t = i / 100.0f;
            NEAR(out.Ease(t), 1.0 - in.Ease(1.0f - t), 1e-5);
        }
        NEAR(io.Ease(0.5f), 0.5, 1e-3);
        NEAR(io.Ease(0.49999f), io.Ease(0.50001f), 0.03);   /* continuous (bounce is steep here) */
        for (int i = 1; i < 100; i++)   // in-out is point-symmetric around (0.5, 0.5)
        {
            float t = i / 100.0f;
            NEAR(io.Ease(t) + io.Ease(1.0f - t), 1.0, 1e-4);
        }
    }

    // analytic values of the ease-in curves at t = 0.5
    NEAR(Make(EasingKind::Quadratic, EasingMode::EaseIn).Ease(0.5f), 0.25, 1e-6);
    NEAR(Make(EasingKind::Cubic, EasingMode::EaseIn).Ease(0.5f), 0.125, 1e-6);
    NEAR(Make(EasingKind::Quartic, EasingMode::EaseIn).Ease(0.5f), 0.0625, 1e-6);
    NEAR(Make(EasingKind::Quintic, EasingMode::EaseIn).Ease(0.5f), 0.03125, 1e-6);
    NEAR(Make(EasingKind::Sine, EasingMode::EaseIn).Ease(0.5f), 1.0 - std::sin(std::acos(-1.0) / 4), 1e-6);
    NEAR(Make(EasingKind::Circle, EasingMode::EaseIn).Ease(0.5f), 1.0 - std::sqrt(0.75), 1e-6);
    NEAR(Make(EasingKind::Back, EasingMode::EaseIn).Ease(0.5f), 0.125 - 0.5 * 1.0 * std::sin(std::acos(-1.0) / 2), 1e-6);
    NEAR(Make(EasingKind::Exponential, EasingMode::EaseIn).Ease(0.5f), (std::exp(1.0) - 1) / (std::exp(2.0) - 1), 1e-6);
    NEAR(Make(EasingKind::Power, EasingMode::EaseIn).Ease(0.5f), 0.25, 1e-6);

    // parameters
    Easing p = Make(EasingKind::Power, EasingMode::EaseIn);
    p.power = 3.0f;
    NEAR(p.Ease(0.5f), 0.125, 1e-6);
    p.power = -2.0f;                    // negative powers are clamped to 0: pow(t, 0) = 1
    NEAR(p.Ease(0.5f), 1.0, 1e-6);
    Easing x = Make(EasingKind::Exponential, EasingMode::EaseIn);
    x.exponent = 0.0f;                  // zero exponent is linear
    NEAR(x.Ease(0.3f), 0.3, 1e-6);
    Easing b = Make(EasingKind::Back, EasingMode::EaseIn);
    b.amplitude = 0.0f;                 // no overshoot: cubic
    NEAR(b.Ease(0.5f), 0.125, 1e-6);
    CHECK(Make(EasingKind::Back, EasingMode::EaseIn).Ease(0.2f) < 0.0f);   // default back overshoots below zero
    CHECK(Make(EasingKind::Elastic, EasingMode::EaseOut).Ease(0.3f) != 0.0f);
    Easing el = Make(EasingKind::Elastic, EasingMode::EaseIn);
    el.oscillations = -4;               // clamped to 0: sin(t * pi/2) shape
    NEAR(el.Ease(1.0f), 1.0, 1e-5);

    // power / polynomial / sine / circle / exponential curves are monotonic in every mode
    const EasingKind mono[] = {EasingKind::Circle, EasingKind::Cubic, EasingKind::Exponential, EasingKind::Power,
                               EasingKind::Quadratic, EasingKind::Quartic, EasingKind::Quintic, EasingKind::Sine};
    for (EasingKind kind : mono)
        for (EasingMode mode : modes)
        {
            Easing e = Make(kind, mode);
            float prev = e.Ease(0.0f);
            for (int i = 1; i <= 1000; i++)
            {
                float v = e.Ease(i / 1000.0f);
                CHECK(v >= prev - 1e-5f);
                prev = v;
            }
        }

    // bounce stays between 0 and 1 (ease-in) and ends at 1; bounciness <= 1 is clamped, not a division by zero
    Easing bo = Make(EasingKind::Bounce, EasingMode::EaseIn);
    for (int i = 0; i <= 1000; i++)
    {
        float v = bo.Ease(i / 1000.0f);
        CHECK(v >= -1e-4f && v <= 1.0001f);
    }
    bo.bounciness = 1.0f;
    CHECK(std::isfinite(bo.Ease(0.5f)));
    bo.bounciness = 0.0f;
    CHECK(std::isfinite(bo.Ease(0.5f)));
    bo.bounces = -3;
    (void)bo.Ease(0.5f);
}

int main()
{
    TestKeySpline();
    TestEasing();
    std::printf("anim_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
