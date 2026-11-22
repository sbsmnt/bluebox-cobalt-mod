// Cobalt Cluster engine: four wavetable oscillators with spread, scan,
// bipolar phase/cross modulation and seeded per-track offsets.
// Inspired by cluster-oscillator voices; independent code, no third-party
// algorithm is copied. Drop-in for a plaits::Engine slot.
//
//   Harmonics -> SPAN   below centre: free detune spread up to +/-12 st.
//                       centre: unison (seed micro-detune only).
//                       above centre: oscillators stacked 1..4 scale degrees.
//   Timbre    -> WARP   centre clean; left: ring phase modulation;
//                       right: exponential cross-modulation, ring-mod at max.
//   Morph     -> MORPH  scans the 16-wave set (each oscillator offset by seed).
//   set_seed()          micro-detune, wave offsets and phases per track.
#ifndef COBALT_CLUSTER_ENGINE_H
#define COBALT_CLUSTER_ENGINE_H

#include <stdint.h>
#include "plaits/dsp/engine/engine.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "cluster_waves.h"

// Cortex-M7 FPv5 has single-instruction floor, min and saturating
// conversions. Every helper below gives exactly the same result as its
// portable C fallback (used by host builds), so audio stays bit-identical.
#if defined(__ARM_ARCH_7EM__) && defined(__ARM_FP) && (__ARM_FP & 8)
#define COBALT_CLUSTER_FPV5 1
#endif

namespace cobalt {

const int kClusterOscillators = 4;

class ClusterEngine : public plaits::Engine {
 public:
  ClusterEngine() { }
  ~ClusterEngine() { }

  virtual void Init(stmlib::BufferAllocator* allocator) {
    (void)allocator;
    seed_ = 0x2545f491u;
    Reset();
  }

  virtual void Reset() {
    for (int i = 0; i < kClusterOscillators; ++i) {
      previous_frequency_[i] = 0.0f;
      previous_sample_[i] = 0.0f;
    }
    previous_pm_ = previous_xm_ = previous_ring_ = 0.0f;
    previous_morph_ = 0.0f;
    dc_in_ = dc_out_ = 0.0f;
    ApplySeed();
  }

  virtual void LoadUserData(const uint8_t* user_data) { (void)user_data; }

  // Deterministic per-track variation. Changing the seed re-rolls detune,
  // wave offsets and starting phases; it costs nothing per sample.
  void set_seed(uint32_t seed) {
    if (seed == seed_) return;
    seed_ = seed;
    ApplySeed();
  }

  virtual void Render(
      const plaits::EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped) {
    (void)already_enveloped;
    // SPAN: per-oscillator pitch offsets in semitones.
    static const float kFreeSpread[kClusterOscillators] = { 0.0f, 1.0f, -1.0f, 0.5f };
    // Natural minor scale, two octaves, for the stacked-degree half.
    static const float kScale[15] = { 0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 20, 22, 24 };
    float span = parameters.harmonics;
    float offsets[kClusterOscillators];
    if (span < 0.5f) {
      float spread = (0.5f - span) * 24.0f;  // 0..12 semitones
      for (int i = 0; i < kClusterOscillators; ++i) offsets[i] = kFreeSpread[i] * spread;
    } else {
      int degree = static_cast<int>((span - 0.5f) * 9.99f);  // 0..4
      for (int i = 0; i < kClusterOscillators; ++i) offsets[i] = kScale[i * degree];
    }

    // WARP: bipolar around centre with a small dead zone.
    float warp = (parameters.timbre - 0.5f) * 2.0f;
    float pm = 0.0f, xm = 0.0f, ring = 0.0f;
    if (warp < -0.02f) {
      pm = (-warp - 0.02f) * 1.02f;           // up to ~1 cycle
      pm *= pm;
    } else if (warp > 0.02f) {
      float w = (warp - 0.02f) * 1.02f;
      xm = w * w * 3.0f;                      // up to 3 octaves
      ring = w > 0.75f ? (w - 0.75f) * 4.0f : 0.0f;
    }

    // MORPH: base scan position across the wave set.
    float morph = parameters.morph * static_cast<float>(kClusterWaves - 1);

    float frequency[kClusterOscillators];
    for (int i = 0; i < kClusterOscillators; ++i) {
      float f = plaits::NoteToFrequency(parameters.note + offsets[i] + detune_[i]);
      frequency[i] = f > 0.45f ? 0.45f : f;
    }

    stmlib::ParameterInterpolator pm_mod(&previous_pm_, pm, size);
    stmlib::ParameterInterpolator xm_mod(&previous_xm_, xm, size);
    stmlib::ParameterInterpolator ring_mod(&previous_ring_, ring, size);
    stmlib::ParameterInterpolator morph_mod(&previous_morph_, morph, size);
    stmlib::ParameterInterpolator f0(&previous_frequency_[0], frequency[0], size);
    stmlib::ParameterInterpolator f1(&previous_frequency_[1], frequency[1], size);
    stmlib::ParameterInterpolator f2(&previous_frequency_[2], frequency[2], size);
    stmlib::ParameterInterpolator f3(&previous_frequency_[3], frequency[3], size);

    // Band-limit level per oscillator for this block (block-rate choice).
    const float* level[kClusterOscillators];
    for (int i = 0; i < kClusterOscillators; ++i) {
      level[i] = &cluster_waves[frequency[i] < (1.0f / 64.0f) ? 0 : 1][0][0];
    }

    bool use_pm = pm > 0.0f || previous_pm_ > 0.0f;
    bool use_xm = xm > 0.0f || previous_xm_ > 0.0f;
    if (use_pm && use_xm) {
      RenderLoop<true, true>(out, size, level, pm_mod, xm_mod, ring_mod, morph_mod, f0, f1, f2, f3);
    } else if (use_pm) {
      RenderLoop<true, false>(out, size, level, pm_mod, xm_mod, ring_mod, morph_mod, f0, f1, f2, f3);
    } else if (use_xm) {
      RenderLoop<false, true>(out, size, level, pm_mod, xm_mod, ring_mod, morph_mod, f0, f1, f2, f3);
    } else {
      RenderLoop<false, false>(out, size, level, pm_mod, xm_mod, ring_mod, morph_mod, f0, f1, f2, f3);
    }
    if (aux) {
      for (size_t i = 0; i < size; ++i) aux[i] = out[i];
    }
  }

 private:
  // 2^x for |x| <= ~8 with a cubic fraction polynomial (about 0.1% error).
  static inline float Exp2(float x) {
#ifdef COBALT_CLUSTER_FPV5
    float floor_x, floor_i;
    int e;
    __asm("vrintm.f32 %0, %1" : "=t"(floor_x) : "t"(x));
    __asm("vcvtm.s32.f32 %0, %1" : "=t"(floor_i) : "t"(x));
    __asm("vmov %0, %1" : "=r"(e) : "t"(floor_i));
    float f = x - floor_x;
#else
    int e = static_cast<int>(x);
    if (static_cast<float>(e) > x) --e;
    float f = x - static_cast<float>(e);
#endif
    float p = 1.0f + f * (0.6951786f + f * (0.2261898f + f * 0.0782315f));
    union { float f; uint32_t u; } v = { p };
    v.u += static_cast<uint32_t>(e) << 23;
    return v.f;
  }

  static inline float MinFrequency(float f) {
#ifdef COBALT_CLUSTER_FPV5
    float r;
    __asm("vminnm.f32 %0, %1, %2" : "=t"(r) : "t"(f), "t"(0.45f));
    return r;
#else
    return f > 0.45f ? 0.45f : f;
#endif
  }

  // Truncating float-to-int32 that saturates at the int32 limits (ARM VCVT
  // semantics; the deep-PM sound the previews were approved with).
  static inline int32_t SaturateToInt32(float x) {
#ifdef COBALT_CLUSTER_FPV5
    float r;
    int32_t i;
    __asm("vcvt.s32.f32 %0, %1" : "=t"(r) : "t"(x));
    __asm("vmov %0, %1" : "=r"(i) : "t"(r));
    return i;
#else
    return x >= 2147483648.0f ? INT32_MAX
        : x < -2147483648.0f ? INT32_MIN : static_cast<int32_t>(x);
#endif
  }

  // Low 24 phase bits as a fraction in [0, 1); exact either way.
  static inline float PhaseFraction(uint32_t ph) {
#ifdef COBALT_CLUSTER_FPV5
    float r;
    __asm("vmov %0, %1\n\tvcvt.f32.u32 %0, %0, #24" : "=&t"(r) : "r"(ph & 0xffffffu));
    return r;
#else
    return static_cast<float>(ph & 0xffffff) * (1.0f / 16777216.0f);
#endif
  }

  template<bool PM, bool XM>
  void RenderLoop(
      float* out, size_t size, const float* const* level,
      stmlib::ParameterInterpolator& pm_mod,
      stmlib::ParameterInterpolator& xm_mod,
      stmlib::ParameterInterpolator& ring_mod,
      stmlib::ParameterInterpolator& morph_mod,
      stmlib::ParameterInterpolator& f0,
      stmlib::ParameterInterpolator& f1,
      stmlib::ParameterInterpolator& f2,
      stmlib::ParameterInterpolator& f3) {
    const float kPhaseScale = 4294967296.0f;
    // Wave pair and blend are chosen once per block (morph is smoothed
    // between blocks); phases are 32-bit so wrap and PM cost nothing.
    float morph = morph_mod.Next();
    for (size_t n = 1; n < size; ++n) morph_mod.Next();
    const float* wa[kClusterOscillators];
    const float* wb[kClusterOscillators];
    float blend[kClusterOscillators];
    const float top = static_cast<float>(kClusterWaves - 1) - 0.001f;
    for (int i = 0; i < kClusterOscillators; ++i) {
      float position = morph + wave_offset_[i];
      position = position > top ? top : position;
      int w = static_cast<int>(position);
      blend[i] = position - static_cast<float>(w);
      wa[i] = level[i] + w * (kClusterWaveSize + 1);
      wb[i] = w < kClusterWaves - 1 ? wa[i] + (kClusterWaveSize + 1) : wa[i];
    }
    uint32_t phase[kClusterOscillators];
    float s[kClusterOscillators];
    float dc_in = dc_in_, dc_out = dc_out_;
    for (int i = 0; i < kClusterOscillators; ++i) {
      phase[i] = phase_[i];
      s[i] = previous_sample_[i];
    }
    for (size_t n = 0; n < size; ++n) {
      float pm = PM ? pm_mod.Next() * kPhaseScale : 0.0f;
      float xm = XM ? xm_mod.Next() : 0.0f;
      float ring = XM ? ring_mod.Next() : 0.0f;
      float f[kClusterOscillators] = { f0.Next(), f1.Next(), f2.Next(), f3.Next() };
      float sum = 0.0f;
      for (int i = 0; i < kClusterOscillators; ++i) {
        // Each oscillator is modulated by its neighbour (a ring of four).
        float m = s[(i + kClusterOscillators - 1) & (kClusterOscillators - 1)];
        float fi = f[i];
        if (XM) {
          fi = MinFrequency(fi * Exp2(xm * m));
        }
        phase[i] += static_cast<uint32_t>(fi * kPhaseScale);
        uint32_t ph = phase[i];
        if (PM) {
          // Saturate deep PM exactly as ARM's float-to-int conversion does
          // (the sound the previews were approved with), without C++ UB.
          ph += static_cast<uint32_t>(SaturateToInt32(pm * m));
        }
        uint32_t index = ph >> 24;
        float t = PhaseFraction(ph);
        const float* a = wa[i] + index;
        const float* b = wb[i] + index;
        // Table values are whole int16 numbers stored as floats, so these
        // differences are exact, as the former integer subtraction was.
        float sa = a[0] + (a[1] - a[0]) * t;
        float sb = b[0] + (b[1] - b[0]) * t;
        s[i] = (sa + (sb - sa) * blend[i]) * (1.0f / 32768.0f);
        sum += s[i];
      }
      float mix = sum * 0.25f;
      if (XM) mix += ring * ((s[0] * s[1] + s[2] * s[3]) * 0.5f - mix);
      // ~10 Hz DC blocker: phase/cross-modulation skews the waveform.
      dc_out = mix - dc_in + 0.9987f * dc_out;
      dc_in = mix;
      out[n] = dc_out;
    }
    for (int i = 0; i < kClusterOscillators; ++i) {
      phase_[i] = phase[i];
      previous_sample_[i] = s[i];
    }
    dc_in_ = dc_in;
    dc_out_ = dc_out;
  }

  void ApplySeed() {
    uint32_t x = seed_ * 2654435761u + 0x9e3779b9u;
    for (int i = 0; i < kClusterOscillators; ++i) {
      x = x * 1664525u + 1013904223u;
      // +/-8 cents micro-detune (oscillator 0 stays on pitch).
      detune_[i] = i ? (static_cast<float>(x >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.16f : 0.0f;
      x = x * 1664525u + 1013904223u;
      wave_offset_[i] = i ? static_cast<float>(x >> 8) * (2.5f / 16777216.0f) : 0.0f;
      x = x * 1664525u + 1013904223u;
      phase_[i] = x;
    }
  }

  uint32_t seed_;
  uint32_t phase_[kClusterOscillators];
  float previous_sample_[kClusterOscillators];
  float previous_frequency_[kClusterOscillators];
  float detune_[kClusterOscillators];
  float wave_offset_[kClusterOscillators];
  float previous_pm_, previous_xm_, previous_ring_, previous_morph_;
  float dc_in_, dc_out_;
};

}  // namespace cobalt

#endif  // COBALT_CLUSTER_ENGINE_H
