#pragma once

#include <cstdint>
#include <span>

namespace bounce::util
{

/** Mixes a 64-bit value (SplitMix64 finaliser). Used to derive independent sub-seeds. */
constexpr uint64_t mixSeed (uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/** Derives a seed for a named sub-stream (e.g. one per bar or per part), so locking or
    regenerating one part never changes the random numbers another part sees. */
constexpr uint64_t deriveSeed (uint64_t seed, uint64_t stream) noexcept
{
    return mixSeed (seed ^ mixSeed (stream + 0x632BE59BD9B4E019ull));
}

/** PCG32 generator. Deliberately avoids <random> distributions, whose output differs between
    standard libraries: the same seed must produce the same idea on Windows, macOS and Linux. */
class Random
{
public:
    explicit Random (uint64_t seed = 0x853C49E6748FEA9Bull) noexcept { reseed (seed); }

    void reseed (uint64_t seed) noexcept
    {
        state = 0;
        inc = (mixSeed (seed) << 1u) | 1u;
        nextUInt32();
        state += mixSeed (seed ^ 0xDA3E39CB94B95BDBull);
        nextUInt32();
    }

    uint32_t nextUInt32() noexcept
    {
        const uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        const auto xorshifted = static_cast<uint32_t> (((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<uint32_t> (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    /** Uniform integer in [0, n). Returns 0 for n <= 0. Unbiased. */
    int nextInt (int n) noexcept
    {
        if (n <= 1)
            return 0;
        const auto bound = static_cast<uint32_t> (n);
        const uint32_t threshold = (0u - bound) % bound;
        for (;;)
        {
            const uint32_t r = nextUInt32();
            if (r >= threshold)
                return static_cast<int> (r % bound);
        }
    }

    /** Uniform integer in [lo, hi]. */
    int nextIntInclusive (int lo, int hi) noexcept { return lo + nextInt (hi - lo + 1); }

    /** Uniform double in [0, 1). */
    double nextDouble() noexcept { return (nextUInt32() >> 5) * (1.0 / 134217728.0); }

    /** Uniform double in [-1, 1). */
    double nextBipolar() noexcept { return nextDouble() * 2.0 - 1.0; }

    /** Approximately normal(0, 1) (Irwin-Hall, 4 samples): cheap and fully deterministic. */
    double nextGaussianish() noexcept
    {
        double s = 0.0;
        for (int i = 0; i < 4; ++i)
            s += nextDouble();
        return (s - 2.0) * 1.7320508075688772; // sqrt(12 / 4)
    }

    bool chance (double probability) noexcept { return nextDouble() < probability; }

    /** Picks an index with probability proportional to weights (negative weights count as 0).
        Returns -1 when every weight is zero. */
    int weightedIndex (std::span<const double> weights) noexcept
    {
        double total = 0.0;
        for (double w : weights)
            total += w > 0.0 ? w : 0.0;
        if (total <= 0.0)
            return -1;

        double r = nextDouble() * total;
        int last = -1;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            if (weights[i] <= 0.0)
                continue;
            last = static_cast<int> (i);
            if (r < weights[i])
                return last;
            r -= weights[i];
        }
        return last;
    }

private:
    uint64_t state = 0;
    uint64_t inc = 1;
};

} // namespace bounce::util
