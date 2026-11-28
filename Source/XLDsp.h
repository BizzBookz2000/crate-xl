/*
    XL-950  -  Sampler Filter / Tape Processor
    Copyright (c) 2026 ILLAHTAINMENT. All rights reserved.

    DSP building blocks. Everything here runs at the (2x oversampled) internal rate.
*/
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace xl
{
constexpr float kPi = 3.14159265358979f;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

/** Tiny fast PRNG for tape hiss. */
struct Noise
{
    uint32_t state = 0x1234567u;
    float next() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (float) (int32_t) state * (1.0f / 2147483648.0f);
    }
};

//==============================================================================
/** Andrew Simper's zero-delay-feedback state variable filter (one channel). */
struct SVF
{
    float ic1 = 0, ic2 = 0;
    float low = 0, band = 0, high = 0;

    struct Coeffs { float a1 = 1, a2 = 0, a3 = 0, k = 1.4142f; };

    static Coeffs make (float freq, float q, float sampleRate) noexcept
    {
        Coeffs c;
        const float g = std::tan (kPi * std::min (freq, sampleRate * 0.49f) / sampleRate);
        c.k  = 1.0f / q;
        c.a1 = 1.0f / (1.0f + g * (g + c.k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }

    void process (float x, const Coeffs& c) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2; band = v1; high = x - c.k * v1 - v2;
    }

    void reset() noexcept { ic1 = ic2 = low = band = high = 0; }
};

//==============================================================================
/** RBJ biquad, transposed direct form II. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};

    void setPeak (float f, float q, float gainDb, float fs) noexcept
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w = 2.0f * kPi * f / fs, cw = std::cos (w), al = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + al / A;
        b0 = (1.0f + al * A) / a0; b1 = -2.0f * cw / a0; b2 = (1.0f - al * A) / a0;
        a1 = -2.0f * cw / a0;       a2 = (1.0f - al / A) / a0;
    }
    void setLowpass (float f, float q, float fs) noexcept
    {
        const float w = 2.0f * kPi * std::min (f, fs * 0.45f) / fs, cw = std::cos (w), al = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + al;
        b0 = (1.0f - cw) * 0.5f / a0; b1 = (1.0f - cw) / a0; b2 = b0;
        a1 = -2.0f * cw / a0;         a2 = (1.0f - al) / a0;
    }
    void setHighpass (float f, float q, float fs) noexcept
    {
        const float w = 2.0f * kPi * f / fs, cw = std::cos (w), al = std::sin (w) / (2.0f * q);
        const float a0 = 1.0f + al;
        b0 = (1.0f + cw) * 0.5f / a0; b1 = -(1.0f + cw) / a0; b2 = b0;
        a1 = -2.0f * cw / a0;         a2 = (1.0f - al) / a0;
    }
    float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

//==============================================================================
/** 12-bit / reduced-rate sample-and-hold, the grainy front end of late-80s samplers. */
struct TwelveBit
{
    float phase[2] { 1, 1 }, held[2] {};

    float process (float x, int ch, float holdRate, float fs) noexcept
    {
        phase[ch] += holdRate / fs;
        if (phase[ch] >= 1.0f)
        {
            phase[ch] -= 1.0f;
            const float clipped = std::clamp (x, -1.0f, 1.0f);
            held[ch] = std::round (clipped * 2047.0f) / 2047.0f;   // 12-bit converter
        }
        return held[ch];
    }
    void reset() noexcept { phase[0] = phase[1] = 1; held[0] = held[1] = 0; }
};

//==============================================================================
/** 4-pole (24 dB/oct) transistor-style low pass: four ZDF one-pole stages with a
    soft-clipped input and gentle global feedback for that rounded, slightly
    "pushed" sampler-filter knee. Resonance is fixed and low on purpose. */
struct FourPoleLP
{
    float s[2][4] {};
    float lastOut[2] {};

    static constexpr float resonance = 0.55f;   // 0 .. 4 (4 = self oscillation)

    static float softClip (float x) noexcept { return 1.6f * std::tanh (x / 1.6f); }

    float process (float x, int ch, float G /* g/(1+g) */) noexcept
    {
        float in = softClip ((x * (1.0f + resonance)) - resonance * lastOut[ch]);
        for (int i = 0; i < 4; ++i)
        {
            const float v = (in - s[ch][i]) * G;
            const float y = v + s[ch][i];
            s[ch][i] = y + v;
            in = y;
        }
        lastOut[ch] = in;
        return in;
    }
    void reset() noexcept { for (auto& c : s) for (auto& v : c) v = 0; lastOut[0] = lastOut[1] = 0; }
};

//==============================================================================
/** Tape stage modeled after a 1/4" 8-track portastudio running at 7.5 ips:
    wow & flutter, asymmetric saturation, head bump, HF loss and a whisper of hiss. */
struct TapeStage
{
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        const int size = (int) (fs * 0.02f) + 4;
        for (auto& b : delay) b.assign ((size_t) size, 0.0f);
        writePos = 0;
        wowPhase = flutterPhase = 0;
        drift = 0;
        headBump.reset(); hfLoss.reset(); subsonic.reset(); hissFilt[0] = hissFilt[1] = 0;
        subsonic.setHighpass (22.0f, 0.707f, fs);
        setAmount (0.0f);
    }

    static constexpr float baseDelayMs = 2.0f;
    int latencySamples() const { return (int) std::round (baseDelayMs * 0.001f * fs); }

    void setAmount (float a) noexcept
    {
        amount = a;
        drive    = dbToGain (a * 18.0f);
        makeup   = std::pow (drive, -0.85f);
        bias     = 0.12f * a;
        biasComp = std::tanh (bias);
        wet      = std::min (1.0f, a * 5.0f);
        headBump.setPeak (80.0f, 0.9f, a * 3.0f, fs);
        hfLoss.setLowpass (21000.0f - a * 9000.0f, 0.6f, fs);
    }

    /** Advance the shared transport modulation once per sample, before processing channels. */
    void tick() noexcept
    {
        wowPhase     += 0.55f / fs;  if (wowPhase >= 1.0f) wowPhase -= 1.0f;
        flutterPhase += 6.3f  / fs;  if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;
        drift += (noise.next() * 0.5f - drift) * (2.0f / fs);       // slow random speed drift
        const float modMs = amount * (0.22f * std::sin (2 * kPi * wowPhase)
                                    + 0.025f * std::sin (2 * kPi * flutterPhase)
                                    + 0.08f * drift);
        currentDelay = (baseDelayMs + modMs) * 0.001f * fs;
        writePos = (writePos + 1) % (int) delay[0].size();
    }

    float process (float x, int ch) noexcept
    {
        auto& buf = delay[(size_t) ch];
        const int size = (int) buf.size();
        buf[(size_t) writePos] = x;

        // Cubic (Hermite) interpolated read for wow/flutter
        float readPos = (float) writePos - currentDelay;
        while (readPos < 0) readPos += (float) size;
        const int i1 = (int) readPos;
        const float f = readPos - (float) i1;
        const float y0 = buf[(size_t) ((i1 - 1 + size) % size)], y1 = buf[(size_t) i1];
        const float y2 = buf[(size_t) ((i1 + 1) % size)],        y3 = buf[(size_t) ((i1 + 2) % size)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        const float dry = ((c3 * f + c2) * f + c1) * f + y1;

        if (wet <= 0.0f)
            return dry;

        // Record path: head bump -> asymmetric saturation -> HF loss -> subsonic -> hiss
        float t = headBump.process (dry, ch);
        t = (std::tanh (t * drive + bias) - biasComp) * makeup;
        t = hfLoss.process (t, ch);
        t = subsonic.process (t, ch);

        const float n = noise.next();
        hissFilt[ch] += 0.35f * (n - hissFilt[ch]);
        t += (n - hissFilt[ch]) * dbToGainFast * amount;

        return dry + wet * (t - dry);
    }

private:
    float fs = 88200.0f, amount = 0, drive = 1, makeup = 1, bias = 0, biasComp = 0, wet = 0;
    float wowPhase = 0, flutterPhase = 0, drift = 0, currentDelay = 0;
    static constexpr float dbToGainFast = 0.00008f;   // ~ -82 dBFS hiss at full tape
    std::vector<float> delay[2];
    int writePos = 0;
    Biquad headBump, hfLoss, subsonic;
    float hissFilt[2] {};
    Noise noise;
};

} // namespace xl
