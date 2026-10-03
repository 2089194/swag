#include "bounce/analysis/Fft.h"

#include <cmath>
#include <utility>

namespace bounce::analysis
{

Fft::Fft (int order) : n (1 << order)
{
    const double pi = 3.14159265358979323846;
    twiddles.resize (static_cast<size_t> (n / 2));
    for (int k = 0; k < n / 2; ++k)
        twiddles[static_cast<size_t> (k)] = std::polar (1.0f, static_cast<float> (-2.0 * pi * k / n));

    bitrev.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        bitrev[static_cast<size_t> (i)] = r;
    }

    window.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        window[static_cast<size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / n));
    scratch.resize (static_cast<size_t> (n));
}

void Fft::forward (std::vector<std::complex<float>>& d) const
{
    for (int i = 0; i < n; ++i)
        if (const int j = bitrev[static_cast<size_t> (i)]; j > i)
            std::swap (d[static_cast<size_t> (i)], d[static_cast<size_t> (j)]);

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2;
        const int step = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; ++k)
            {
                const auto w = twiddles[static_cast<size_t> (k * step)];
                auto& a = d[static_cast<size_t> (i + k)];
                auto& b = d[static_cast<size_t> (i + k + half)];
                const auto t = w * b;
                b = a - t;
                a = a + t;
            }
    }
}

void Fft::magnitudes (const float* frame, std::vector<float>& out) const
{
    for (int i = 0; i < n; ++i)
        scratch[static_cast<size_t> (i)] = { frame[i] * window[static_cast<size_t> (i)], 0.0f };
    forward (scratch);
    out.resize (static_cast<size_t> (n / 2 + 1));
    for (int k = 0; k <= n / 2; ++k)
        out[static_cast<size_t> (k)] = std::abs (scratch[static_cast<size_t> (k)]);
}

} // namespace bounce::analysis
