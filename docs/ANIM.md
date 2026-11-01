# Animation math

`src/anim` holds the two numeric building blocks of XAML animation, adapted from the MIT-licensed
Microsoft.UI.Xaml sources (see PROVENANCE.md for the files and the list of changes):

* `KeySpline` - the cubic Bezier with fixed end points (0,0) and (1,1) that shapes keyframe progress.
  `GetSplineProgress(x)` returns Y for a linear progress X using the upstream pseudo-Newton search
  (accuracy about 0.001 in X); the search is bounded to 200 iterations. Control points must be in
  [0,1] (`IsValid`). Not thread-safe (it caches the last solution).
* `Easing` - the eleven XAML easing curves (Back, Bounce, Circle, Cubic, Elastic, Exponential,
  Power, Quadratic, Quartic, Quintic, Sine) with the modes EaseIn, EaseOut and EaseInOut and the
  parameter defaults of the XAML classes.

Tests (`tests/anim_tests.cpp`) do not copy upstream results: they check endpoints, the relations between the modes
(out(t) = 1 - in(1-t), point symmetry of in-out), analytic values, parameter edge cases (negative power,
zero exponent, zero amplitude, negative oscillations and bounces, bounciness at or below 1), monotonicity,
and the key spline against an independent bisection solver in double precision on eight curves
(including the CSS `ease` reference point 0.8024 at x = 0.5).
