#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

/** Lock-free FIFO between the network thread (push) and the audio thread (pull).

    The phone's audio clock and the host's never match exactly, so instead of
    skipping or stalling when the fill level drifts, pull() plays very slightly
    faster or slower (at most 0.5%) to steer it back to the target. The same
    resampler converts the phone's 48 kHz to whatever rate the host runs at.
*/
class DriftBuffer
{
public:
    static constexpr double sourceRate = 48000.0;

    DriftBuffer() : ring (capacity, 0.0f) {}

    /** Network thread. */
    void push (const int16_t* samples, int n)
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);

        // Only happens if the host stopped pulling; pull() skips ahead when it resumes.
        if (w - r + (uint64_t) n > capacity - 8)
            return;

        for (int i = 0; i < n; ++i)
            ring[(size_t) ((w + (uint64_t) i) & mask)] = (float) samples[i] * (1.0f / 32768.0f);

        writePos.store (w + (uint64_t) n, std::memory_order_release);
    }

    /** Network thread: a new phone connected, start from a clean slate. */
    void requestReset() { resetRequested.store (true, std::memory_order_release); }

    /** Audio thread. Always fills `out` (with silence while buffering). */
    void pull (float* out, int n, double hostRate, double targetMs)
    {
        const auto w = writePos.load (std::memory_order_acquire);
        auto r = readPos.load (std::memory_order_relaxed);

        if (resetRequested.exchange (false, std::memory_order_acq_rel))
        {
            r = w;
            priming = true;
        }

        const double target = sourceRate * targetMs / 1000.0;
        const double maxFill = std::max (target * 4.0, sourceRate * 0.25);
        double avail = (double) (w - r);

        if (avail > maxFill)
        {
            // Fell far behind (host was paused, or a long network stall just flushed).
            r = w - (uint64_t) target;
            avail = target;
            frac = 0.0;
            avgFill = target;
            drops.fetch_add (1, std::memory_order_relaxed);
        }

        if (priming && avail >= target)
        {
            priming = false;
            frac = 0.0;
            avgFill = target;
        }

        bool ok = ! priming;

        if (ok)
        {
            // ~1 s smoothing so packet-sized steps in the fill level don't wobble the speed.
            const double alpha = 1.0 - std::exp (-(double) n / hostRate);
            avgFill += alpha * (avail - avgFill);
            const double error = (avgFill - target) / target;
            const double ratio = (sourceRate / hostRate) * (1.0 + std::clamp (error * 0.01, -0.005, 0.005));

            const double lastPos = frac + (double) (n - 1) * ratio;

            if ((double) ((uint64_t) lastPos + 2) > avail)
            {
                underruns.fetch_add (1, std::memory_order_relaxed);
                priming = true;
                ok = false;
            }
            else
            {
                double pos = frac;

                for (int i = 0; i < n; ++i)
                {
                    const auto ip = (uint64_t) pos;
                    const auto t = (float) (pos - (double) ip);
                    const float a = ring[(size_t) ((r + ip) & mask)];
                    const float b = ring[(size_t) ((r + ip + 1) & mask)];
                    out[i] = a + (b - a) * t;
                    pos += ratio;
                }

                const auto used = (uint64_t) pos;
                frac = pos - (double) used;
                r += used;
            }
        }

        if (! ok)
            std::fill (out, out + n, 0.0f);

        readPos.store (r, std::memory_order_release);
        fillMs.store ((float) ((double) (w - r) * 1000.0 / sourceRate), std::memory_order_relaxed);
    }

    float getFillMs() const   { return fillMs.load (std::memory_order_relaxed); }
    int getUnderruns() const  { return underruns.load (std::memory_order_relaxed); }
    int getDrops() const      { return drops.load (std::memory_order_relaxed); }

private:
    static constexpr uint64_t capacity = 1u << 18; // ~5.4 s at 48 kHz
    static constexpr uint64_t mask = capacity - 1;

    std::vector<float> ring;
    std::atomic<uint64_t> writePos { 0 }, readPos { 0 };
    std::atomic<bool> resetRequested { false };
    std::atomic<float> fillMs { 0.0f };
    std::atomic<int> underruns { 0 }, drops { 0 };

    // Audio-thread state.
    bool priming = true;
    double frac = 0.0;
    double avgFill = 0.0;
};
