// scroll_core.h - AOSP-style touch scroll physics (CY-95 chunk C1). No Arduino/ESP includes, float + int32,
// header-only, no heap. Everything is a pure function of wall-clock time, so any frame rate gives the same
// positions. Source: OverScroller.SplineOverScroller (spline fling, BALLISTIC overfling, CUBIC springback),
// ViewConfiguration constants at 143 ppi, EdgeEffect-like spring (docs/research/cy95-scroll-feel.md 1.1, 1.2, 5.1).
#pragma once
#include <math.h>
#include <stdint.h>

namespace cydscroll {

// Tunables (knobs for the later debug command). Units: px, px/s, s.
struct ScrollCfg {
  float ppi = 143.0f;               // panel density; sets the fling distance scale
  float friction = 0.015f;          // ViewConfiguration.getScrollFriction()
  float minFling = 45.0f;           // MINIMUM_FLING_VELOCITY 50 dp/s at 143 ppi; slower releases do not fling
  float maxFling = 7150.0f;         // MAXIMUM_FLING_VELOCITY 8000 dp/s at 143 ppi; faster releases are clamped
  float overflingPx = 5.0f;         // OVERFLING_DISTANCE 6 dp; max overshoot past a bound
  float gravity = 2000.0f;          // px/s^2, BALLISTIC and CUBIC springback deceleration (AOSP GRAVITY)
  float springStiffness = 24.657f;  // Edge spring stiffness (unit mass, 1/s^2)
  float springDamping = 0.98f;      // Edge spring damping ratio (<1: slight overshoot)
  float edgeRangePx = 60.0f;        // Edge::dampen asymptote (largest rubber-band offset)
  float settlePx = 0.5f;            // Edge spring is finished when its envelope falls below this
};

namespace detail {
constexpr float kDecel = 2.3582f;  // ln(0.78)/ln(0.9)
constexpr float kInflexion = 0.35f;
constexpr float kP1 = 0.175f;      // START_TENSION(0.5) * INFLEXION
constexpr float kP2 = 0.35f;       // 1 - END_TENSION(1.0) * (1 - INFLEXION)

inline float sgn(float v) { return v < 0 ? -1.0f : 1.0f; }

// Cubic Bezier f(x) = 3a x(1-x)^2 + 3b x^2(1-x) + x^3 and its derivative.
inline float bez(float x, float a, float b) {
  const float m = 1 - x;
  return 3 * a * x * m * m + 3 * b * x * x * m + x * x * x;
}
inline float dbez(float x, float a, float b) {
  const float m = 1 - x;
  return 3 * a * m * m + 6 * (b - a) * x * m + 3 * (1 - b) * x * x;
}

// Spline position coefficient (0..1) at normalised time t and its slope d(coef)/dt: solve time(x) = t by
// bisection (control values P1/P2 in time, 0.5/1.0 in position), then evaluate position(x).
inline void spline(float t, float* coef, float* slope) {
  if (t <= 0) { *coef = 0; *slope = dbez(0, 0.5f, 1.0f) / dbez(0, kP1, kP2); return; }
  if (t >= 1) { *coef = 1; *slope = 0; return; }
  float lo = 0, hi = 1, x = 0.5f;
  for (int i = 0; i < 24; ++i) {
    x = 0.5f * (lo + hi);
    if (bez(x, kP1, kP2) < t) lo = x; else hi = x;
  }
  *coef = bez(x, 0.5f, 1.0f);
  const float dt = dbez(x, kP1, kP2);
  *slope = dt > 1e-6f ? dbez(x, 0.5f, 1.0f) / dt : 0;
}
}  // namespace detail

class Fling {
 public:
  explicit Fling(const ScrollCfg& c = ScrollCfg()) : c_(c) {}
  const ScrollCfg& cfg() const { return c_; }

  // Start at y0 with release velocity v (px/s, sign = direction of position change), within [minY,maxY].
  // |v| < minFling: no motion. Start outside the bounds: springback only. |v| > maxFling is clamped.
  void start(float y0, float v, float minY, float maxY, uint32_t nowMs) {
    t0_ = nowMs; y0_ = y0; min_ = minY; max_ = maxY;
    dist_ = durS_ = tSpl_ = tBal_ = cubS_ = over_ = vb_ = bound_ = 0; dir_ = 1;
    endY_ = y0 < minY ? minY : (y0 > maxY ? maxY : y0);
    if (y0 < minY || y0 > maxY) {
      bound_ = endY_; dir_ = y0 > maxY ? 1.0f : -1.0f; over_ = fabsf(y0 - bound_);
      cubS_ = sqrtf(2 * over_ / c_.gravity);
      return;
    }
    float av = fabsf(v);
    if (av < c_.minFling) return;
    if (av > c_.maxFling) av = c_.maxFling;
    dir_ = detail::sgn(v);
    const float coeff = c_.friction * 9.80665f * 39.37f * c_.ppi * 0.84f;
    const float l = logf(detail::kInflexion * av / coeff);
    durS_ = expf(l / (detail::kDecel - 1));
    dist_ = coeff * expf(detail::kDecel / (detail::kDecel - 1) * l);
    tSpl_ = tBal_ = durS_;
    endY_ = y0 + dir_ * dist_;
    const float room = dir_ > 0 ? maxY - y0 : y0 - minY;  // distance to the bound ahead
    if (dist_ <= room) return;
    // Reaches the bound: find when (coefficient is monotonic), then overfling and springback.
    bound_ = dir_ > 0 ? maxY : minY; endY_ = bound_;
    float lo = 0, hi = 1, cf, sl;
    for (int i = 0; i < 24; ++i) {
      const float m = 0.5f * (lo + hi);
      detail::spline(m, &cf, &sl);
      if (cf * dist_ < room) lo = m; else hi = m;
    }
    detail::spline(hi, &cf, &sl);
    tSpl_ = hi * durS_;
    vb_ = sl * dist_ / durS_;                       // speed on reaching the bound
    float ov = vb_ * vb_ / (2 * c_.gravity);        // BALLISTIC stopping distance
    float tb = vb_ / c_.gravity;
    if (ov > c_.overflingPx) {                      // cut off at the overfling cap
      ov = c_.overflingPx;
      tb = (vb_ - sqrtf(vb_ * vb_ - 2 * c_.gravity * ov)) / c_.gravity;
    }
    over_ = ov; tBal_ = tSpl_ + tb; cubS_ = sqrtf(2 * ov / c_.gravity);
  }

  // Flywheel: a new flick during a fling adds to the current velocity if same direction, else replaces it.
  void flywheel(float v, uint32_t nowMs) {
    float nv = v;
    if (!finished(nowMs)) { const float cur = velocity(nowMs); if (cur * v > 0) nv += cur; }
    start(position(nowMs), nv, min_, max_, nowMs);
  }

  float position(uint32_t nowMs) const {
    const float e = elapsed(nowMs);
    if (e >= tBal_ + cubS_) return endY_;
    if (e < tSpl_) {
      float cf, sl; detail::spline(e / durS_, &cf, &sl);
      return y0_ + dir_ * cf * dist_;
    }
    if (e < tBal_) { const float s = e - tSpl_; return bound_ + dir_ * (vb_ * s - 0.5f * c_.gravity * s * s); }
    const float u = (e - tBal_) / cubS_;
    return bound_ + dir_ * over_ * (1 - (3 * u * u - 2 * u * u * u));
  }

  float velocity(uint32_t nowMs) const {  // px/s, signed
    const float e = elapsed(nowMs);
    if (e >= tBal_ + cubS_) return 0;
    if (e < tSpl_) { float cf, sl; detail::spline(e / durS_, &cf, &sl); return dir_ * sl * dist_ / durS_; }
    if (e < tBal_) return dir_ * (vb_ - c_.gravity * (e - tSpl_));
    const float u = (e - tBal_) / cubS_;
    return -dir_ * over_ * 6 * (u - u * u) / cubS_;
  }

  bool finished(uint32_t nowMs) const { return elapsed(nowMs) >= tBal_ + cubS_; }
  float finalY() const { return endY_; }
  float durationMs() const { return (tBal_ + cubS_) * 1000.0f; }  // spline + overfling + springback

 private:
  float elapsed(uint32_t nowMs) const { return (float)(int32_t)(nowMs - t0_) * 0.001f; }
  ScrollCfg c_;
  uint32_t t0_ = 0;
  float y0_ = 0, min_ = 0, max_ = 0, bound_ = 0, endY_ = 0, dir_ = 1;
  float dist_ = 0, durS_ = 0, tSpl_ = 0, tBal_ = 0, cubS_ = 0, over_ = 0, vb_ = 0;
};

// Edge spring: overscroll offset (px past the bound) released with a velocity; underdamped spring in closed
// form (EdgeEffect-like, no integrator, any frame rate).
class Edge {
 public:
  explicit Edge(const ScrollCfg& c = ScrollCfg()) : c_(c) {}
  const ScrollCfg& cfg() const { return c_; }
  // Rubber band: raw finger pull past the bound (px) -> displayed offset, saturating at edgeRangePx.
  float dampen(float pull) const { return pull <= 0 ? 0 : c_.edgeRangePx * pull / (pull + c_.edgeRangePx); }
  // Release from `offset` px with outward velocity `vel` px/s.
  void settle(float offset, float vel, uint32_t nowMs) {
    t0_ = nowMs; x0_ = offset; v0_ = vel;
    w_ = sqrtf(c_.springStiffness); z_ = c_.springDamping;
    wd_ = w_ * sqrtf(1 - z_ * z_);
    const float b = (v0_ + z_ * w_ * x0_) / wd_;
    const float amp = sqrtf(x0_ * x0_ + b * b);
    endS_ = amp <= c_.settlePx ? 0 : logf(amp / c_.settlePx) / (z_ * w_);
  }
  float offset(uint32_t nowMs) const {
    const float t = el(nowMs);
    if (t >= endS_) return 0;
    return expf(-z_ * w_ * t) * (x0_ * cosf(wd_ * t) + (v0_ + z_ * w_ * x0_) / wd_ * sinf(wd_ * t));
  }
  bool finished(uint32_t nowMs) const { return el(nowMs) >= endS_; }

 private:
  float el(uint32_t nowMs) const { return (float)(int32_t)(nowMs - t0_) * 0.001f; }
  ScrollCfg c_;
  uint32_t t0_ = 0;
  float x0_ = 0, v0_ = 0, w_ = 1, z_ = 1, wd_ = 1, endS_ = 0;
};

}  // namespace cydscroll
