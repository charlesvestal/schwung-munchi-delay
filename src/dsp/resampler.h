/* resampler.h -- streaming stereo sample-rate converter.
 *
 * The TAPE engine runs at its native 48 kHz, so every constant in it (filter
 * coefficients, the 450-sample minimum delay, the looper's 6000-sample scrub
 * period, the 300-sample click fade, the samples themselves) means what it
 * meant on the hardware. Move's audio runs at 44.1 kHz; these convert at the
 * boundary, in both directions.
 *
 * Polyphase windowed sinc: 32 taps, 256 phases, linear phase interpolation.
 * Cutoff sits just under the lower of the two Nyquists.
 */
#pragma once
#include <math.h>
#include <string.h>
#include <stdint.h>

namespace munchi
{

class StreamResampler
{
  public:
    static constexpr int kTaps   = 32;
    static constexpr int kPhases = 256;
    static constexpr int kHist   = 4096; // ring of input frames (power of 2)

    void Init(double in_rate, double out_rate)
    {
        step_ = in_rate / out_rate; // input frames per output frame
        double cutoff = 0.92 * (in_rate < out_rate ? 1.0 : out_rate / in_rate);
        for(int p = 0; p <= kPhases; p++)
        {
            double frac = (double)p / kPhases;
            double sum  = 0.0;
            for(int t = 0; t < kTaps; t++)
            {
                // tap t sits at input offset (t - kTaps/2 + 1) from the base
                double x = (double)(t - kTaps / 2 + 1) - frac;
                double s = fabs(x) < 1e-12
                               ? 1.0
                               : sin(M_PI * x * cutoff) / (M_PI * x * cutoff);
                double w = 0.5 * (1.0 + cos(M_PI * x / (kTaps / 2)));
                if(fabs(x) >= kTaps / 2)
                    w = 0.0;
                table_[p][t] = (float)(s * w);
                sum += s * w;
            }
            for(int t = 0; t < kTaps; t++)
                table_[p][t] = (float)(table_[p][t] / sum);
        }
        Reset();
    }

    void Reset()
    {
        memset(hl_, 0, sizeof(hl_));
        memset(hr_, 0, sizeof(hr_));
        write_ = kTaps; // pre-roll of silence so the first outputs have history
        pos_   = (double)(kTaps / 2);
    }

    /** Frames of input still needed before `n` more outputs can be made. */
    int InputNeeded(int n) const
    {
        double need_end = pos_ + step_ * (n - 1) + kTaps / 2 + 1;
        long   avail    = (long)write_;
        long   need     = (long)ceil(need_end) - avail;
        return need > 0 ? (int)need : 0;
    }

    void Push(float l, float r)
    {
        hl_[write_ & (kHist - 1)] = l;
        hr_[write_ & (kHist - 1)] = r;
        write_++;
    }

    /** Can one output frame be produced from what has been pushed? */
    bool CanPull() const { return pos_ + kTaps / 2 + 1 <= (double)write_; }

    void Pull(float *l, float *r)
    {
        long   base = (long)floor(pos_);
        double frac = pos_ - (double)base;
        double fp   = frac * kPhases;
        int    p    = (int)fp;
        float  pf   = (float)(fp - p);
        float  al = 0.f, ar = 0.f, bl = 0.f, br = 0.f;
        long   start = base - kTaps / 2 + 1;
        for(int t = 0; t < kTaps; t++)
        {
            long  i  = (start + t) & (kHist - 1);
            float xl = hl_[i], xr = hr_[i];
            al += xl * table_[p][t];
            ar += xr * table_[p][t];
            bl += xl * table_[p + 1][t];
            br += xr * table_[p + 1][t];
        }
        *l = al + (bl - al) * pf;
        *r = ar + (br - ar) * pf;
        pos_ += step_;
    }

  private:
    double step_ = 1.0;
    double pos_  = 0.0;
    uint64_t write_ = 0;
    float  hl_[kHist], hr_[kHist];
    float  table_[kPhases + 1][kTaps];
};

} // namespace munchi
