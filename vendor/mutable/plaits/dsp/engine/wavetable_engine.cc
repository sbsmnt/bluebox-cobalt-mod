// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// 8x8x3 wave terrain.

#include "plaits/dsp/engine/wavetable_engine.h"

#include <algorithm>

#include "plaits/resources.h"

namespace plaits {

using namespace std;
using namespace stmlib;

const int kNumBanks = 4;
const int kNumWavesPerBank = 64;
const int kNumWaves = 192;
const int kNumCustomWaves = 15;

const size_t kTableSize = 128;
const float kTableSizeF = float(kTableSize);

// Cobalt: the factory map is identical for every track and needs no RAM.
// Explicit constant addresses require no static constructor or startup copy.
static const int16_t* const builtin_wave_map[256] = {
  wav_integrated_waves + 0 * (kTableSize + 4),
  wav_integrated_waves + 1 * (kTableSize + 4),
  wav_integrated_waves + 2 * (kTableSize + 4),
  wav_integrated_waves + 3 * (kTableSize + 4),
  wav_integrated_waves + 4 * (kTableSize + 4),
  wav_integrated_waves + 5 * (kTableSize + 4),
  wav_integrated_waves + 6 * (kTableSize + 4),
  wav_integrated_waves + 7 * (kTableSize + 4),
  wav_integrated_waves + 8 * (kTableSize + 4),
  wav_integrated_waves + 9 * (kTableSize + 4),
  wav_integrated_waves + 10 * (kTableSize + 4),
  wav_integrated_waves + 11 * (kTableSize + 4),
  wav_integrated_waves + 12 * (kTableSize + 4),
  wav_integrated_waves + 13 * (kTableSize + 4),
  wav_integrated_waves + 14 * (kTableSize + 4),
  wav_integrated_waves + 15 * (kTableSize + 4),
  wav_integrated_waves + 16 * (kTableSize + 4),
  wav_integrated_waves + 17 * (kTableSize + 4),
  wav_integrated_waves + 18 * (kTableSize + 4),
  wav_integrated_waves + 19 * (kTableSize + 4),
  wav_integrated_waves + 20 * (kTableSize + 4),
  wav_integrated_waves + 21 * (kTableSize + 4),
  wav_integrated_waves + 22 * (kTableSize + 4),
  wav_integrated_waves + 23 * (kTableSize + 4),
  wav_integrated_waves + 24 * (kTableSize + 4),
  wav_integrated_waves + 25 * (kTableSize + 4),
  wav_integrated_waves + 26 * (kTableSize + 4),
  wav_integrated_waves + 27 * (kTableSize + 4),
  wav_integrated_waves + 28 * (kTableSize + 4),
  wav_integrated_waves + 29 * (kTableSize + 4),
  wav_integrated_waves + 30 * (kTableSize + 4),
  wav_integrated_waves + 31 * (kTableSize + 4),
  wav_integrated_waves + 32 * (kTableSize + 4),
  wav_integrated_waves + 33 * (kTableSize + 4),
  wav_integrated_waves + 34 * (kTableSize + 4),
  wav_integrated_waves + 35 * (kTableSize + 4),
  wav_integrated_waves + 36 * (kTableSize + 4),
  wav_integrated_waves + 37 * (kTableSize + 4),
  wav_integrated_waves + 38 * (kTableSize + 4),
  wav_integrated_waves + 39 * (kTableSize + 4),
  wav_integrated_waves + 40 * (kTableSize + 4),
  wav_integrated_waves + 41 * (kTableSize + 4),
  wav_integrated_waves + 42 * (kTableSize + 4),
  wav_integrated_waves + 43 * (kTableSize + 4),
  wav_integrated_waves + 44 * (kTableSize + 4),
  wav_integrated_waves + 45 * (kTableSize + 4),
  wav_integrated_waves + 46 * (kTableSize + 4),
  wav_integrated_waves + 47 * (kTableSize + 4),
  wav_integrated_waves + 48 * (kTableSize + 4),
  wav_integrated_waves + 49 * (kTableSize + 4),
  wav_integrated_waves + 50 * (kTableSize + 4),
  wav_integrated_waves + 51 * (kTableSize + 4),
  wav_integrated_waves + 52 * (kTableSize + 4),
  wav_integrated_waves + 53 * (kTableSize + 4),
  wav_integrated_waves + 54 * (kTableSize + 4),
  wav_integrated_waves + 55 * (kTableSize + 4),
  wav_integrated_waves + 56 * (kTableSize + 4),
  wav_integrated_waves + 57 * (kTableSize + 4),
  wav_integrated_waves + 58 * (kTableSize + 4),
  wav_integrated_waves + 59 * (kTableSize + 4),
  wav_integrated_waves + 60 * (kTableSize + 4),
  wav_integrated_waves + 61 * (kTableSize + 4),
  wav_integrated_waves + 62 * (kTableSize + 4),
  wav_integrated_waves + 63 * (kTableSize + 4),
  wav_integrated_waves + 64 * (kTableSize + 4),
  wav_integrated_waves + 65 * (kTableSize + 4),
  wav_integrated_waves + 66 * (kTableSize + 4),
  wav_integrated_waves + 67 * (kTableSize + 4),
  wav_integrated_waves + 68 * (kTableSize + 4),
  wav_integrated_waves + 69 * (kTableSize + 4),
  wav_integrated_waves + 70 * (kTableSize + 4),
  wav_integrated_waves + 71 * (kTableSize + 4),
  wav_integrated_waves + 72 * (kTableSize + 4),
  wav_integrated_waves + 73 * (kTableSize + 4),
  wav_integrated_waves + 74 * (kTableSize + 4),
  wav_integrated_waves + 75 * (kTableSize + 4),
  wav_integrated_waves + 76 * (kTableSize + 4),
  wav_integrated_waves + 77 * (kTableSize + 4),
  wav_integrated_waves + 78 * (kTableSize + 4),
  wav_integrated_waves + 79 * (kTableSize + 4),
  wav_integrated_waves + 80 * (kTableSize + 4),
  wav_integrated_waves + 81 * (kTableSize + 4),
  wav_integrated_waves + 82 * (kTableSize + 4),
  wav_integrated_waves + 83 * (kTableSize + 4),
  wav_integrated_waves + 84 * (kTableSize + 4),
  wav_integrated_waves + 85 * (kTableSize + 4),
  wav_integrated_waves + 86 * (kTableSize + 4),
  wav_integrated_waves + 87 * (kTableSize + 4),
  wav_integrated_waves + 88 * (kTableSize + 4),
  wav_integrated_waves + 89 * (kTableSize + 4),
  wav_integrated_waves + 90 * (kTableSize + 4),
  wav_integrated_waves + 91 * (kTableSize + 4),
  wav_integrated_waves + 92 * (kTableSize + 4),
  wav_integrated_waves + 93 * (kTableSize + 4),
  wav_integrated_waves + 94 * (kTableSize + 4),
  wav_integrated_waves + 95 * (kTableSize + 4),
  wav_integrated_waves + 96 * (kTableSize + 4),
  wav_integrated_waves + 97 * (kTableSize + 4),
  wav_integrated_waves + 98 * (kTableSize + 4),
  wav_integrated_waves + 99 * (kTableSize + 4),
  wav_integrated_waves + 100 * (kTableSize + 4),
  wav_integrated_waves + 101 * (kTableSize + 4),
  wav_integrated_waves + 102 * (kTableSize + 4),
  wav_integrated_waves + 103 * (kTableSize + 4),
  wav_integrated_waves + 104 * (kTableSize + 4),
  wav_integrated_waves + 105 * (kTableSize + 4),
  wav_integrated_waves + 106 * (kTableSize + 4),
  wav_integrated_waves + 107 * (kTableSize + 4),
  wav_integrated_waves + 108 * (kTableSize + 4),
  wav_integrated_waves + 109 * (kTableSize + 4),
  wav_integrated_waves + 110 * (kTableSize + 4),
  wav_integrated_waves + 111 * (kTableSize + 4),
  wav_integrated_waves + 112 * (kTableSize + 4),
  wav_integrated_waves + 113 * (kTableSize + 4),
  wav_integrated_waves + 114 * (kTableSize + 4),
  wav_integrated_waves + 115 * (kTableSize + 4),
  wav_integrated_waves + 116 * (kTableSize + 4),
  wav_integrated_waves + 117 * (kTableSize + 4),
  wav_integrated_waves + 118 * (kTableSize + 4),
  wav_integrated_waves + 119 * (kTableSize + 4),
  wav_integrated_waves + 120 * (kTableSize + 4),
  wav_integrated_waves + 121 * (kTableSize + 4),
  wav_integrated_waves + 122 * (kTableSize + 4),
  wav_integrated_waves + 123 * (kTableSize + 4),
  wav_integrated_waves + 124 * (kTableSize + 4),
  wav_integrated_waves + 125 * (kTableSize + 4),
  wav_integrated_waves + 126 * (kTableSize + 4),
  wav_integrated_waves + 127 * (kTableSize + 4),
  wav_integrated_waves + 128 * (kTableSize + 4),
  wav_integrated_waves + 129 * (kTableSize + 4),
  wav_integrated_waves + 130 * (kTableSize + 4),
  wav_integrated_waves + 131 * (kTableSize + 4),
  wav_integrated_waves + 132 * (kTableSize + 4),
  wav_integrated_waves + 133 * (kTableSize + 4),
  wav_integrated_waves + 134 * (kTableSize + 4),
  wav_integrated_waves + 135 * (kTableSize + 4),
  wav_integrated_waves + 136 * (kTableSize + 4),
  wav_integrated_waves + 137 * (kTableSize + 4),
  wav_integrated_waves + 138 * (kTableSize + 4),
  wav_integrated_waves + 139 * (kTableSize + 4),
  wav_integrated_waves + 140 * (kTableSize + 4),
  wav_integrated_waves + 141 * (kTableSize + 4),
  wav_integrated_waves + 142 * (kTableSize + 4),
  wav_integrated_waves + 143 * (kTableSize + 4),
  wav_integrated_waves + 144 * (kTableSize + 4),
  wav_integrated_waves + 145 * (kTableSize + 4),
  wav_integrated_waves + 146 * (kTableSize + 4),
  wav_integrated_waves + 147 * (kTableSize + 4),
  wav_integrated_waves + 148 * (kTableSize + 4),
  wav_integrated_waves + 149 * (kTableSize + 4),
  wav_integrated_waves + 150 * (kTableSize + 4),
  wav_integrated_waves + 151 * (kTableSize + 4),
  wav_integrated_waves + 152 * (kTableSize + 4),
  wav_integrated_waves + 153 * (kTableSize + 4),
  wav_integrated_waves + 154 * (kTableSize + 4),
  wav_integrated_waves + 155 * (kTableSize + 4),
  wav_integrated_waves + 156 * (kTableSize + 4),
  wav_integrated_waves + 157 * (kTableSize + 4),
  wav_integrated_waves + 158 * (kTableSize + 4),
  wav_integrated_waves + 159 * (kTableSize + 4),
  wav_integrated_waves + 160 * (kTableSize + 4),
  wav_integrated_waves + 161 * (kTableSize + 4),
  wav_integrated_waves + 162 * (kTableSize + 4),
  wav_integrated_waves + 163 * (kTableSize + 4),
  wav_integrated_waves + 164 * (kTableSize + 4),
  wav_integrated_waves + 165 * (kTableSize + 4),
  wav_integrated_waves + 166 * (kTableSize + 4),
  wav_integrated_waves + 167 * (kTableSize + 4),
  wav_integrated_waves + 168 * (kTableSize + 4),
  wav_integrated_waves + 169 * (kTableSize + 4),
  wav_integrated_waves + 170 * (kTableSize + 4),
  wav_integrated_waves + 171 * (kTableSize + 4),
  wav_integrated_waves + 172 * (kTableSize + 4),
  wav_integrated_waves + 173 * (kTableSize + 4),
  wav_integrated_waves + 174 * (kTableSize + 4),
  wav_integrated_waves + 175 * (kTableSize + 4),
  wav_integrated_waves + 176 * (kTableSize + 4),
  wav_integrated_waves + 177 * (kTableSize + 4),
  wav_integrated_waves + 178 * (kTableSize + 4),
  wav_integrated_waves + 179 * (kTableSize + 4),
  wav_integrated_waves + 180 * (kTableSize + 4),
  wav_integrated_waves + 181 * (kTableSize + 4),
  wav_integrated_waves + 182 * (kTableSize + 4),
  wav_integrated_waves + 183 * (kTableSize + 4),
  wav_integrated_waves + 184 * (kTableSize + 4),
  wav_integrated_waves + 185 * (kTableSize + 4),
  wav_integrated_waves + 186 * (kTableSize + 4),
  wav_integrated_waves + 187 * (kTableSize + 4),
  wav_integrated_waves + 188 * (kTableSize + 4),
  wav_integrated_waves + 189 * (kTableSize + 4),
  wav_integrated_waves + 190 * (kTableSize + 4),
  wav_integrated_waves + 191 * (kTableSize + 4),
  wav_integrated_waves + 0 * (kTableSize + 4),
  wav_integrated_waves + 101 * (kTableSize + 4),
  wav_integrated_waves + 10 * (kTableSize + 4),
  wav_integrated_waves + 111 * (kTableSize + 4),
  wav_integrated_waves + 20 * (kTableSize + 4),
  wav_integrated_waves + 121 * (kTableSize + 4),
  wav_integrated_waves + 30 * (kTableSize + 4),
  wav_integrated_waves + 131 * (kTableSize + 4),
  wav_integrated_waves + 40 * (kTableSize + 4),
  wav_integrated_waves + 141 * (kTableSize + 4),
  wav_integrated_waves + 50 * (kTableSize + 4),
  wav_integrated_waves + 151 * (kTableSize + 4),
  wav_integrated_waves + 60 * (kTableSize + 4),
  wav_integrated_waves + 161 * (kTableSize + 4),
  wav_integrated_waves + 70 * (kTableSize + 4),
  wav_integrated_waves + 171 * (kTableSize + 4),
  wav_integrated_waves + 80 * (kTableSize + 4),
  wav_integrated_waves + 181 * (kTableSize + 4),
  wav_integrated_waves + 90 * (kTableSize + 4),
  wav_integrated_waves + 191 * (kTableSize + 4),
  wav_integrated_waves + 100 * (kTableSize + 4),
  wav_integrated_waves + 9 * (kTableSize + 4),
  wav_integrated_waves + 110 * (kTableSize + 4),
  wav_integrated_waves + 19 * (kTableSize + 4),
  wav_integrated_waves + 120 * (kTableSize + 4),
  wav_integrated_waves + 29 * (kTableSize + 4),
  wav_integrated_waves + 130 * (kTableSize + 4),
  wav_integrated_waves + 39 * (kTableSize + 4),
  wav_integrated_waves + 140 * (kTableSize + 4),
  wav_integrated_waves + 49 * (kTableSize + 4),
  wav_integrated_waves + 150 * (kTableSize + 4),
  wav_integrated_waves + 59 * (kTableSize + 4),
  wav_integrated_waves + 160 * (kTableSize + 4),
  wav_integrated_waves + 69 * (kTableSize + 4),
  wav_integrated_waves + 170 * (kTableSize + 4),
  wav_integrated_waves + 79 * (kTableSize + 4),
  wav_integrated_waves + 180 * (kTableSize + 4),
  wav_integrated_waves + 89 * (kTableSize + 4),
  wav_integrated_waves + 190 * (kTableSize + 4),
  wav_integrated_waves + 99 * (kTableSize + 4),
  wav_integrated_waves + 8 * (kTableSize + 4),
  wav_integrated_waves + 109 * (kTableSize + 4),
  wav_integrated_waves + 18 * (kTableSize + 4),
  wav_integrated_waves + 119 * (kTableSize + 4),
  wav_integrated_waves + 28 * (kTableSize + 4),
  wav_integrated_waves + 129 * (kTableSize + 4),
  wav_integrated_waves + 38 * (kTableSize + 4),
  wav_integrated_waves + 139 * (kTableSize + 4),
  wav_integrated_waves + 48 * (kTableSize + 4),
  wav_integrated_waves + 149 * (kTableSize + 4),
  wav_integrated_waves + 58 * (kTableSize + 4),
  wav_integrated_waves + 159 * (kTableSize + 4),
  wav_integrated_waves + 68 * (kTableSize + 4),
  wav_integrated_waves + 169 * (kTableSize + 4),
  wav_integrated_waves + 78 * (kTableSize + 4),
  wav_integrated_waves + 179 * (kTableSize + 4),
  wav_integrated_waves + 88 * (kTableSize + 4),
  wav_integrated_waves + 189 * (kTableSize + 4),
  wav_integrated_waves + 98 * (kTableSize + 4),
  wav_integrated_waves + 7 * (kTableSize + 4),
  wav_integrated_waves + 108 * (kTableSize + 4),
  wav_integrated_waves + 17 * (kTableSize + 4),
  wav_integrated_waves + 118 * (kTableSize + 4),
  wav_integrated_waves + 27 * (kTableSize + 4)
};

void WavetableEngine::Init(BufferAllocator* allocator) {
  phase_ = 0.0f;

  x_lp_ = 0.0f;
  y_lp_ = 0.0f;
  z_lp_ = 0.0f;
  
  x_pre_lp_ = 0.0f;
  y_pre_lp_ = 0.0f;
  z_pre_lp_ = 0.0f;

  previous_x_ = 0.0f;
  previous_y_ = 0.0f;
  previous_z_ = 0.0f;
  previous_f0_ = a0;

  diff_out_.Init();
  
  // Cobalt: LoadUserData fills all four 64-wave banks, not just one bank.
  wave_map_ = allocator
      ? allocator->Allocate<const int16_t*>(kNumBanks * kNumWavesPerBank)
      : builtin_wave_map;
}

void WavetableEngine::Reset() {
  
}

void WavetableEngine::LoadUserData(const uint8_t* user_data) {
  // The no-allocator factory-map mode is deliberately immutable.
  if (wave_map_ == builtin_wave_map) return;
  for (int bank = 0; bank < kNumBanks; ++bank) {
    for (int wave = 0; wave < kNumWavesPerBank; ++wave) {
      int i = bank * kNumWavesPerBank + wave;

      int w = i;
      if (bank == kNumBanks - 1) {
        w = user_data ? user_data[wave] : (w * 101 % kNumWaves);
      }

      const int16_t* base = wav_integrated_waves;
      if (w >= kNumWaves) {
        base = (const int16_t*)(user_data + 64);
        w = min(w - kNumWaves, kNumCustomWaves);
      }
      const_cast<const int16_t**>(wave_map_)[i] = base + size_t(w) * (kTableSize + 4);
    }
  }
}

inline float Clamp(float x, float amount) {
  x = x - 0.5f;
  x *= amount;
  CONSTRAIN(x, -0.5f, 0.5f);
  x += 0.5f;
  return x;
}

inline float WavetableEngine::ReadWave(
    int x,
    int y,
    int z,
    int phase_integral,
    float phase_fractional) {
  return InterpolateWaveHermite(
      wave_map_[x + y * 8 + z * kNumWavesPerBank],
      phase_integral,
      phase_fractional);
}

void WavetableEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  
  ONE_POLE(x_pre_lp_, parameters.timbre * 6.9999f, 0.2f);
  ONE_POLE(y_pre_lp_, parameters.morph * 6.9999f, 0.2f);
  ONE_POLE(z_pre_lp_, parameters.harmonics * 6.9999f, 0.05f);
  
  const float x = x_pre_lp_;
  const float y = y_pre_lp_;
  const float z = z_pre_lp_;
  
  const float quantization = min(max(z - 3.0f, 0.0f), 1.0f);
  const float lp_coefficient = min(
      max(2.0f * f0 * (4.0f - 3.0f * quantization), 0.01f), 0.1f);
  
  MAKE_INTEGRAL_FRACTIONAL(x);
  MAKE_INTEGRAL_FRACTIONAL(y);
  MAKE_INTEGRAL_FRACTIONAL(z);
  
  x_fractional += quantization * (Clamp(x_fractional, 16.0f) - x_fractional);
  y_fractional += quantization * (Clamp(y_fractional, 16.0f) - y_fractional);
  z_fractional += quantization * (Clamp(z_fractional, 16.0f) - z_fractional);
  
  ParameterInterpolator x_modulation(
      &previous_x_, static_cast<float>(x_integral) + x_fractional, size);
  ParameterInterpolator y_modulation(
      &previous_y_, static_cast<float>(y_integral) + y_fractional, size);
  ParameterInterpolator z_modulation(
      &previous_z_, static_cast<float>(z_integral) + z_fractional, size);

  ParameterInterpolator f0_modulation(&previous_f0_, f0, size);
  
  while (size--) {
    const float f0 = f0_modulation.Next();
    
    const float gain = (1.0f / (f0 * 131072.0f)) * (0.95f - f0);
    const float cutoff = min(kTableSizeF * f0, 1.0f);
    
    ONE_POLE(x_lp_, x_modulation.Next(), lp_coefficient);
    ONE_POLE(y_lp_, y_modulation.Next(), lp_coefficient);
    ONE_POLE(z_lp_, z_modulation.Next(), lp_coefficient);
    
    const float x = x_lp_;
    const float y = y_lp_;
    const float z = z_lp_;

    MAKE_INTEGRAL_FRACTIONAL(x);
    MAKE_INTEGRAL_FRACTIONAL(y);
    MAKE_INTEGRAL_FRACTIONAL(z);

    phase_ += f0;
    if (phase_ >= 1.0f) {
      phase_ -= 1.0f;
    }
    
    const float p = phase_ * kTableSizeF;
    MAKE_INTEGRAL_FRACTIONAL(p);
    
    {
      int x0 = x_integral;
      int x1 = x_integral + 1;
      int y0 = y_integral;
      int y1 = y_integral + 1;
      int z0 = z_integral;
      int z1 = z_integral + 1;
      
      if (z0 >= 4) {
        z0 = 7 - z0;
      }
      if (z1 >= 4) {
        z1 = 7 - z1;
      }
      
      float x0y0z0 = ReadWave(x0, y0, z0, p_integral, p_fractional);
      float x1y0z0 = ReadWave(x1, y0, z0, p_integral, p_fractional);
      float xy0z0 = x0y0z0 + (x1y0z0 - x0y0z0) * x_fractional;

      float x0y1z0 = ReadWave(x0, y1, z0, p_integral, p_fractional);
      float x1y1z0 = ReadWave(x1, y1, z0, p_integral, p_fractional);
      float xy1z0 = x0y1z0 + (x1y1z0 - x0y1z0) * x_fractional;

      float xyz0 = xy0z0 + (xy1z0 - xy0z0) * y_fractional;

      float x0y0z1 = ReadWave(x0, y0, z1, p_integral, p_fractional);
      float x1y0z1 = ReadWave(x1, y0, z1, p_integral, p_fractional);
      float xy0z1 = x0y0z1 + (x1y0z1 - x0y0z1) * x_fractional;

      float x0y1z1 = ReadWave(x0, y1, z1, p_integral, p_fractional);
      float x1y1z1 = ReadWave(x1, y1, z1, p_integral, p_fractional);
      float xy1z1 = x0y1z1 + (x1y1z1 - x0y1z1) * x_fractional;
      
      float xyz1 = xy0z1 + (xy1z1 - xy0z1) * y_fractional;

      float mix = xyz0 + (xyz1 - xyz0) * z_fractional;
      mix = diff_out_.Process(cutoff, mix) * gain;
      *out++ = mix;
      if (aux) {
        *aux++ = static_cast<float>(static_cast<int>(mix * 32.0f)) / 32.0f;
      }
    }
  }
}

}  // namespace plaits
