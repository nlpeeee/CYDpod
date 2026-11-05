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
  float slopPx = 7.0f;              // touch slop: finger travel before a touch becomes a drag (7 px at 143 ppi)
  uint32_t horizonMs = 100;         // velocity window (VelocityTracker HORIZON)
  uint32_t stopMs = 40;             // ASSUME_POINTER_STOPPED_TIME (Velocity class only; Tracker uses the CY-107 knobs below)
  // CY-107 flick estimator (raw positions). Segment speed s = dy/dt, v += (1-exp(-dt/tauSmooth))*(s-v).
  uint32_t gapMs = 100;             // sample gap tolerated; a longer gap restarts the estimate
  float tauSmooth = 0.050f;         // s, time constant of the velocity smoothing
  uint32_t aggMs = 20;              // samples closer than this to the last used one are aggregated into the next segment
  float minTravelPx = 12.0f;        // net travel since the last reset needed to fling
  float minVelPxS = 80.0f;          // slower releases do not fling
  float reverseMinPx = 2.0f;        // a segment against v of at least this many px resets the estimate
  // Rest detection (separate from sample gaps): no fling when the finger sat still this long after moving > restMovePx.
  uint32_t restMs = 100;            // KNOWN AMBIGUITY: rests of ~80-100 ms look like a sparse sample gap and may still coast
  float restMovePx = 2.0f;          // movement smaller than this does not restart the rest timer (jitter)
  // CY-107 coast. Speeds in rows/s (rowPx per row). Friction time constant tau(v): tauSlow at and below slowRows/s,
  // rising linearly in speed to tauFast at fastRows/s and above. Position is a closed form of time (no integrator).
  float rowPx = 40.0f;              // list row height the row-based knobs refer to
  float capRowsPerSec = 120.0f;     // release speed cap
  float slowRows = 10.0f;           // tau = tauSlow at and below this speed
  float fastRows = 60.0f;           // tau = tauFast at and above this speed
  float tauSlow = 0.32f;            // s
  float tauFast = 4.0f;             // s
  float stopPxS = 40.0f;            // the coast ends when its speed falls to this
  uint32_t minAnimMs = 33;          // a started coast is never finished sooner than one frame after release
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
  void stop(uint32_t nowMs) { start(position(nowMs), 0, min_, max_, nowMs); }  // tap-to-stop: freeze here (outside bounds: springback)
  float durationMs() const { return (tBal_ + cubS_) * 1000.0f; }  // spline + overfling + springback

 private:
  float elapsed(uint32_t nowMs) const { return (float)(int32_t)(nowMs - t0_) * 0.001f; }
  ScrollCfg c_;
  uint32_t t0_ = 0;
  float y0_ = 0, min_ = 0, max_ = 0, bound_ = 0, endY_ = 0, dir_ = 1;
  float dist_ = 0, durS_ = 0, tSpl_ = 0, tBal_ = 0, cubS_ = 0, over_ = 0, vb_ = 0;
};

// CY-107 coast: speed-dependent friction, closed form of time, hard clamp at the list ends (no overfling), ends when
// the speed falls to stopPxS. dv/dt = -v/tau(v) with tau = a + b v between slowRows and fastRows, so
//   time(vh->vl) = a ln(vh/vl) + b (vh-vl),  distance = a (vh-vl) + b/2 (vh^2-vl^2),
// and plain exponentials (b = 0) in the two end regions. Starts at the release time and animates forward (no backdating).
class Coast {
 public:
  explicit Coast(const ScrollCfg& c = ScrollCfg()) : c_(c) {}
  const ScrollCfg& cfg() const { return c_; }
  // v: content velocity in px/s (sign = direction of position change). |v| <= stopPxS or no room: no motion.
  void start(float y0, float v, float minY, float maxY, uint32_t nowMs) {
    t0_ = nowMs; y0_ = y0; dur_ = dist_ = 0; moving_ = false;
    const float cap = c_.capRowsPerSec * c_.rowPx;
    float av = fabsf(v);
    if (av > cap) av = cap;
    dir_ = v < 0 ? -1.0f : 1.0f;
    const float room = dir_ > 0 ? maxY - y0 : y0 - minY;
    if (av <= c_.stopPxS || room <= 0) return;
    v0_ = av; moving_ = true;
    span(v0_, c_.stopPxS, &dur_, &dist_);
    if (dist_ > room) {  // reaches the list end first: find when
      float lo = c_.stopPxS, hi = v0_, t, x;
      for (int i = 0; i < 24; ++i) { const float m = 0.5f * (lo + hi); span(v0_, m, &t, &x); if (x > room) lo = m; else hi = m; }
      span(v0_, 0.5f * (lo + hi), &dur_, &x);
      dist_ = room;
    }
  }
  float position(uint32_t nowMs) const {
    if (!moving_) return y0_;
    const float e = elapsed(nowMs);
    if (e >= dur_) return y0_ + dir_ * dist_;
    float x; speedAt(e, &x);
    return y0_ + dir_ * (x < dist_ ? x : dist_);
  }
  float velocity(uint32_t nowMs) const {  // px/s, signed
    if (!moving_ || elapsed(nowMs) >= dur_) return 0;
    return dir_ * speedAt(elapsed(nowMs), nullptr);
  }
  bool finished(uint32_t nowMs) const { return !moving_ || elapsed(nowMs) * 1000.0f >= durationMs(); }
  float finalY() const { return y0_ + dir_ * dist_; }
  float durationMs() const { const float d = dur_ * 1000.0f; return moving_ && d < (float)c_.minAnimMs ? (float)c_.minAnimMs : d; }
  void stop(uint32_t nowMs) { const float p = position(nowMs); start(p, 0, p, p, nowMs); }  // freeze here

 private:
  float elapsed(uint32_t nowMs) const { return (float)(int32_t)(nowMs - t0_) * 0.001f; }
  // Time and distance to decelerate from vh to vl (vh >= vl), summed over the three friction regions.
  void span(float vh, float vl, float* t, float* x) const {
    const float v1 = c_.slowRows * c_.rowPx, v2 = c_.fastRows * c_.rowPx;
    const float b = (c_.tauFast - c_.tauSlow) / (v2 - v1), a = c_.tauSlow - b * v1;
    *t = *x = 0;
    if (vh > v2) { const float lo = vl > v2 ? vl : v2; *t += c_.tauFast * logf(vh / lo); *x += c_.tauFast * (vh - lo); }
    const float mh = vh < v2 ? vh : v2, ml = vl > v1 ? vl : v1;
    if (mh > ml) { *t += a * logf(mh / ml) + b * (mh - ml); *x += a * (mh - ml) + 0.5f * b * (mh * mh - ml * ml); }
    const float lh = vh < v1 ? vh : v1;
    if (lh > vl) { *t += c_.tauSlow * logf(lh / vl); *x += c_.tauSlow * (lh - vl); }
  }
  // Speed after e seconds (time is monotone decreasing in speed, so bisect); optionally the distance covered.
  float speedAt(float e, float* x) const {
    float lo = c_.stopPxS, hi = v0_, t, d;
    for (int i = 0; i < 24; ++i) { const float m = 0.5f * (lo + hi); span(v0_, m, &t, &d); if (t > e) lo = m; else hi = m; }
    const float v = 0.5f * (lo + hi);
    if (x) { span(v0_, v, &t, &d); *x = d; }
    return v;
  }
  ScrollCfg c_;
  uint32_t t0_ = 0;
  float y0_ = 0, dir_ = 1, v0_ = 0, dur_ = 0, dist_ = 0;
  bool moving_ = false;
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

enum class Strategy : uint8_t { Lsq2, Impulse };

// Finger velocity from a ring of (ms,y) samples inside the horizon (VelocityTracker.cpp semantics): a sample more
// than stopMs after the previous one clears the history. Float only, fixed storage.
class Velocity {
 public:
  explicit Velocity(const ScrollCfg& c = ScrollCfg()) : c_(c) {}
  void clear() { n_ = 0; head_ = 0; }
  int count() const { return n_; }
  uint32_t lastMs() const { return n_ ? at(n_ - 1).ms : 0; }
  void add(uint32_t ms, float y) {
    if (n_ && (uint32_t)(ms - lastMs()) > c_.stopMs) clear();
    if (n_ && ms == lastMs()) { r_[(head_ + kN - 1) % kN].y = y; return; }  // same timestamp: keep the newest y
    r_[head_] = {ms, y}; head_ = (head_ + 1) % kN; if (n_ < kN) ++n_;
  }
  int windowCount() const {
    int m = 0;
    while (m < n_ && (uint32_t)(lastMs() - at(n_ - 1 - m).ms) <= c_.horizonMs) ++m;
    return m;
  }
  // px/s of y over the horizon at the newest sample, clamped to [-maxFling, +maxFling].
  float get(Strategy s) const {
    const int m = windowCount();
    const float v = m < 2 ? 0 : (s == Strategy::Lsq2 ? lsq(m) : impulse(m));
    return v > c_.maxFling ? c_.maxFling : (v < -c_.maxFling ? -c_.maxFling : v);
  }

 private:
  static constexpr int kN = 20;
  struct S { uint32_t ms; float y; };
  const S& at(int i) const { return r_[(head_ + kN - n_ + i) % kN]; }  // i-th oldest
  static float energyToV(float w) { return w < 0 ? -sqrtf(-2 * w) : sqrtf(2 * w); }
  // Impulse: kinetic-energy work integral (oldest first, first term halved); v = sign * sqrt(2 |work|).
  float impulse(int m) const {
    const int b = n_ - m;
    if (m == 2) return (at(b + 1).y - at(b).y) / ((float)(at(b + 1).ms - at(b).ms) * 0.001f);
    float work = 0;
    for (int i = 0; i < m - 1; ++i) {
      const S &p = at(b + i), &q = at(b + i + 1);
      if (q.ms == p.ms) continue;
      const float vc = (q.y - p.y) / ((float)(q.ms - p.ms) * 0.001f);
      work += (vc - energyToV(work)) * fabsf(vc);
      if (i == 0) work *= 0.5f;
    }
    return energyToV(work);
  }
  // LSQ2: y = a0 + a1 x + a2 x^2 over x = (ms - newest)/100 in [-1,0], degree min(2, m-1); v = a1 * 10 px/s.
  float lsq(int m) const {
    const int b = n_ - m, k = (m - 1 < 2 ? m - 1 : 2) + 1;
    const uint32_t tn = lastMs(); const float yn = at(n_ - 1).y;
    float a[3][4] = {};
    for (int i = 0; i < m; ++i) {
      const float x = -(float)(tn - at(b + i).ms) * 0.01f, y = at(b + i).y - yn;
      const float xp[5] = {1, x, x * x, x * x * x, x * x * x * x};
      for (int r = 0; r < k; ++r) { for (int c = 0; c < k; ++c) a[r][c] += xp[r + c]; a[r][3] += xp[r] * y; }
    }
    for (int c = 0; c < k; ++c) {  // Gaussian elimination, partial pivoting
      int p = c;
      for (int r = c + 1; r < k; ++r) if (fabsf(a[r][c]) > fabsf(a[p][c])) p = r;
      if (fabsf(a[p][c]) < 1e-9f) return 0;
      for (int j = 0; j < 4; ++j) { const float t = a[c][j]; a[c][j] = a[p][j]; a[p][j] = t; }
      for (int r = c + 1; r < k; ++r) { const float f = a[r][c] / a[c][c]; for (int j = c; j < 4; ++j) a[r][j] -= f * a[c][j]; }
    }
    float co[3] = {};
    for (int r = k - 1; r >= 0; --r) {
      float s = a[r][3]; for (int c = r + 1; c < k; ++c) s -= a[r][c] * co[c];
      co[r] = s / a[r][r];
    }
    return co[1] * 10.0f;
  }
  ScrollCfg c_;
  S r_[kN] = {};
  int n_ = 0, head_ = 0;
};

// CY-107 release-velocity estimator on RAW positions. Samples may be sparse (changed positions only): gaps up to gapMs
// are bridged by a time-normalised smoothing, longer gaps restart. Rest is detected separately from gaps (release()).
class FlickEstimator {
 public:
  explicit FlickEstimator(const ScrollCfg& c = ScrollCfg()) : c_(c) {}
  void clear() { n_ = 0; segs_ = 0; v_ = 0; }
  int windowCount() const { return n_; }                // samples since the last restart/reversal-free run
  uint32_t lastMs() const { return n_ ? lastMs_ : 0; }  // newest accepted sample
  float smoothed() const { return segs_ ? v_ : 0.0f; }  // px/s, signed, before the release gates
  void add(uint32_t ms, float y) {
    if (n_ && (int32_t)(ms - lastMs_) <= 0) return;     // stale or duplicate report
    if (!n_ || ms - lastMs_ > c_.gapMs) { restart(ms, y); return; }
    ++n_; lastMs_ = ms; lastY_ = y;
    if (fabsf(y - restY_) > c_.restMovePx) { restY_ = y; restMs_ = ms; }
    const uint32_t dt = ms - aMs_;
    if (dt < c_.aggMs) return;                          // aggregate into the next segment
    const float dy = y - aY_, s = dy / ((float)dt * 0.001f);
    if (!segs_) v_ = s;
    else if (dy * v_ < 0 && fabsf(dy) >= c_.reverseMinPx) { v_ = s; resetY_ = aY_; }  // reversal: start over
    else v_ += (1.0f - expf(-(float)dt * 0.001f / c_.tauSmooth)) * (s - v_);
    ++segs_; aMs_ = ms; aY_ = y;
  }
  // Release at time ms (last time the finger was seen): signed px/s, or 0 = no fling (rest, stale, short travel,
  // slow). restNowMs (>= ms, 0 = ms) is the rest clock: sparse polls can see a long rest for only a poll or two, so
  // the caller may pass the poll time that noticed the lift minus the press grace.
  float release(uint32_t ms, uint32_t restNowMs = 0) const {
    const uint32_t rn = (int32_t)(restNowMs - ms) > 0 ? restNowMs : ms;
    if (!segs_ || rn - restMs_ > c_.restMs || ms - lastMs_ > c_.gapMs) return 0;
    if (fabsf(lastY_ - resetY_) < c_.minTravelPx) return 0;
    const float v = v_ > c_.maxFling ? c_.maxFling : (v_ < -c_.maxFling ? -c_.maxFling : v_);
    return fabsf(v) < c_.minVelPxS ? 0.0f : v;
  }

 private:
  void restart(uint32_t ms, float y) {
    n_ = 1; segs_ = 0; v_ = 0; lastMs_ = aMs_ = restMs_ = ms; lastY_ = aY_ = restY_ = resetY_ = y;
  }
  ScrollCfg c_;
  uint32_t lastMs_ = 0, aMs_ = 0, restMs_ = 0;
  float lastY_ = 0, aY_ = 0, restY_ = 0, resetY_ = 0, v_ = 0;
  int n_ = 0, segs_ = 0;
};

// Touch gesture: down / move / up. A touch becomes a drag after RAW |dy| > slopPx; the slop is subtracted so delta()
// is 0 at the claim (no jump). Velocity comes from the raw positions (FlickEstimator); delta() uses the filtered
// positions that are painted and hit-tested. move(ms, y) means filtered == raw. Samples enter the estimator only when
// the raw y changed. Touch-down on a running Fling/Coast stops it and marks the touch as a drag, never a tap.
class Tracker {
 public:
  // The Strategy argument is kept for source compatibility; the CY-107 estimator replaced both strategies.
  explicit Tracker(const ScrollCfg& c = ScrollCfg(), Strategy s = Strategy::Lsq2) : c_(c), est_(c), strat_(s) {}
  void setStrategy(Strategy s) { strat_ = s; }
  Strategy strategy() const { return strat_; }

  void down(uint32_t ms, float y, Fling* running = nullptr) { down(ms, y, y); if (running && !running->finished(ms)) { running->stop(ms); stoppedFling_ = true; } }
  void down(uint32_t ms, float y, Coast* running) { down(ms, y, y); if (running && !running->finished(ms)) { running->stop(ms); stoppedFling_ = true; } }
  void down(uint32_t ms, float y, float yRaw) {
    est_.clear(); est_.add(ms, yRaw);
    downY_ = lastY_ = anchor_ = y; downRaw_ = lastRaw_ = yRaw; claimed_ = false; active_ = true; stoppedFling_ = false; upV_ = 0;
  }
  // Returns true once the touch is a drag (claimed). delta() is then the filtered y minus the claim point (slop removed).
  bool move(uint32_t ms, float y) { return move(ms, y, y); }
  bool move(uint32_t ms, float y, float yRaw) {
    if (!active_) return false;
    if (yRaw != lastRaw_) { est_.add(ms, yRaw); lastRaw_ = yRaw; }
    lastY_ = y;
    if (!claimed_ && (stoppedFling_ || fabsf(yRaw - downRaw_) > c_.slopPx)) {
      claimed_ = true;
      if (stoppedFling_) anchor_ = downY_;
      else {  // remove the slop from the filtered travel, never more than the filtered y has actually moved
        const float dir = detail::sgn(yRaw - downRaw_), moved = (y - downY_) * dir;
        anchor_ = downY_ + dir * (moved < 0 ? 0 : (moved < c_.slopPx ? moved : c_.slopPx));
      }
    }
    return claimed_;
  }
  // Release. Starts `fling`/`coast` at content position y0 within [minY,maxY] and returns true when the touch was a
  // drag and the estimator released a velocity (no rest, travel >= minTravelPx, |v| >= minVelPxS). vScale maps finger
  // velocity to content velocity (-1: content moves opposite to a finger moving along +y). restNowMs: see FlickEstimator.
  bool up(uint32_t ms, Fling& fling, float y0, float minY, float maxY, float vScale = -1.0f, uint32_t restNowMs = 0) {
    if (!release(ms, restNowMs)) return false;
    fling.start(y0, vScale * upV_, minY, maxY, ms);
    return true;
  }
  bool up(uint32_t ms, Coast& coast, float y0, float minY, float maxY, float vScale = -1.0f, uint32_t restNowMs = 0) {
    if (!release(ms, restNowMs)) return false;
    coast.start(y0, vScale * upV_, minY, maxY, ms);
    return !coast.finished(ms);
  }
  bool active() const { return active_; }
  bool claimed() const { return claimed_; }
  bool stoppedFling() const { return stoppedFling_; }               // touch-down hit a running fling
  bool isTap() const { return !claimed_ && !stoppedFling_; }        // meaningful at up(); long-press is the caller's
  float delta() const { return claimed_ ? lastY_ - anchor_ : 0; }   // finger travel since the claim point
  float velocity() const { return est_.smoothed(); }                // live smoothed finger velocity (px/s)
  float releaseVelocity() const { return upV_; }                    // velocity seen by the last up()
  const FlickEstimator& samples() const { return est_; }

 private:
  bool release(uint32_t ms, uint32_t restNowMs) {
    active_ = false; upV_ = 0;
    if (!claimed_) return false;
    upV_ = est_.release(ms, restNowMs);
    return upV_ != 0;
  }
  ScrollCfg c_;
  FlickEstimator est_;
  Strategy strat_;
  float downY_ = 0, lastY_ = 0, anchor_ = 0, downRaw_ = 0, lastRaw_ = 0, upV_ = 0;
  bool active_ = false, claimed_ = false, stoppedFling_ = false;
};

}  // namespace cydscroll
