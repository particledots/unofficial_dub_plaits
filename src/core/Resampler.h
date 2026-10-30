// Streaming windowed-sinc resampler (one channel). Used to bring the synth, which runs at the module's sample rate
// (47872.34 Hz, like the firmware), to the host's rate. Same design as the one in Unofficial Plaits Extended
// (32 taps, Blackman window, 1024 fractional phases), but all buffers are reserved in init() so process() does not
// allocate as long as the caller stays within the sizes given there.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace dub {

class SincResampler
{
public:
    static constexpr int kTaps = 32;        // kernel width in input samples
    static constexpr int kPhases = 1024;    // fractional position resolution

    // inRate / outRate: sample rates. maxInput: the most input samples one process() call will be given.
    void init (double inRate, double outRate, size_t maxInput)
    {
        step_ = inRate / outRate;
        bypass_ = std::abs (inRate - outRate) < 1e-6;
        const double cutoff = std::min (1.0, outRate / inRate) * 0.97;   // anti-alias when going down
        table_.assign ((size_t) kPhases * (size_t) kTaps, 0.0f);
        const double pi = 3.14159265358979323846;
        for (int p = 0; p < kPhases; ++p)
        {
            const double frac = (double) p / (double) kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const double d = (double) (k - (kTaps / 2 - 1)) - frac;
                const double x = d * cutoff;
                const double s = std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
                const double w = 0.42 + 0.5 * std::cos (pi * d / (kTaps / 2)) + 0.08 * std::cos (2.0 * pi * d / (kTaps / 2));
                const double h = s * w * cutoff;
                table_[(size_t) p * (size_t) kTaps + (size_t) k] = (float) h;
                sum += h;
            }
            for (int k = 0; k < kTaps; ++k)    // unity DC gain in every phase
                table_[(size_t) p * (size_t) kTaps + (size_t) k] /= (float) sum;
        }
        hist_.clear();
        hist_.reserve ((size_t) kTaps + maxInput + 16);
        reset();
    }

    void reset()
    {
        hist_.assign ((size_t) kTaps, 0.0f);
        pos_ = 0.0;
    }

    // Appends the input to the history and appends every output that can be made to `out`. Returns how many.
    size_t process (const float* in, size_t n, std::vector<float>& out)
    {
        if (bypass_) { out.insert (out.end(), in, in + n); return n; }
        hist_.insert (hist_.end(), in, in + n);
        size_t produced = 0;
        while ((size_t) pos_ + (size_t) kTaps <= hist_.size())
        {
            const size_t i = (size_t) pos_;
            const double frac = pos_ - (double) i;
            int ph = (int) (frac * (double) kPhases);
            if (ph >= kPhases) ph = kPhases - 1;
            const float* h = &table_[(size_t) ph * (size_t) kTaps];
            const float* x = &hist_[i];
            float acc = 0.0f;
            for (int k = 0; k < kTaps; ++k) acc += h[k] * x[k];
            out.push_back (acc);
            pos_ += step_;
            ++produced;
        }
        const size_t drop = (size_t) pos_;
        if (drop > 0)
        {
            hist_.erase (hist_.begin(), hist_.begin() + (std::ptrdiff_t) drop);
            pos_ -= (double) drop;
        }
        return produced;
    }

    // How late the output is compared with the input, in input samples.
    int latencyInputSamples() const { return bypass_ ? 0 : kTaps / 2; }

private:
    std::vector<float> table_, hist_;
    double step_ = 1.0, pos_ = 0.0;
    bool bypass_ = true;
};

} // namespace dub
