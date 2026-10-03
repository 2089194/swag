#pragma once

#include <complex>
#include <vector>

namespace bounce::analysis
{

/** In-place iterative radix-2 FFT. Size must be a power of two. Plenty fast for offline analysis
    and keeps the core free of JUCE/FFTW. */
class Fft
{
public:
    explicit Fft (int order);

    int size() const noexcept { return n; }

    void forward (std::vector<std::complex<float>>& data) const;

    /** Magnitude spectrum (n/2 + 1 bins) of a real frame, with a Hann window applied. */
    void magnitudes (const float* frame, std::vector<float>& out) const;

private:
    int n;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitrev;
    std::vector<float> window;
    mutable std::vector<std::complex<float>> scratch;
};

} // namespace bounce::analysis
