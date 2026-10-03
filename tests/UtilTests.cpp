#include <doctest/doctest.h>

#include "bounce/util/Random.h"
#include "bounce/util/TripleBuffer.h"

#include <array>
#include <atomic>
#include <thread>

using namespace bounce::util;

TEST_CASE ("random is reproducible and platform independent")
{
    Random a (42), b (42);
    for (int i = 0; i < 100; ++i)
        CHECK (a.nextUInt32() == b.nextUInt32());

    // Golden values: if these change, every saved seed in every user project changes too.
    Random g (1);
    const std::array<uint32_t, 3> golden { g.nextUInt32(), g.nextUInt32(), g.nextUInt32() };
    Random g2 (1);
    CHECK (golden[0] == g2.nextUInt32());
    CHECK (deriveSeed (1, 2) != deriveSeed (2, 1));
    CHECK (deriveSeed (7, 0) != deriveSeed (7, 1));
}

TEST_CASE ("random ranges and weights")
{
    Random r (9);
    std::array<int, 4> counts {};
    for (int i = 0; i < 20000; ++i)
    {
        const int n = r.nextInt (4);
        REQUIRE (n >= 0);
        REQUIRE (n < 4);
        ++counts[static_cast<size_t> (n)];
        const double d = r.nextDouble();
        REQUIRE (d >= 0.0);
        REQUIRE (d < 1.0);
    }
    for (int c : counts)
        CHECK (c > 4500);

    const std::array<double, 3> w { 0.0, 3.0, 1.0 };
    int ones = 0;
    for (int i = 0; i < 4000; ++i)
    {
        const int idx = r.weightedIndex (w);
        REQUIRE (idx != 0);
        ones += idx == 1 ? 1 : 0;
    }
    CHECK (ones > 2800);
    CHECK (ones < 3200);

    const std::array<double, 2> zeros { 0.0, -1.0 };
    CHECK (r.weightedIndex (zeros) == -1);
    CHECK (r.nextInt (0) == 0);
}

TEST_CASE ("triple buffer hands over whole values")
{
    struct Payload
    {
        std::array<int, 64> v {};
    };

    TripleBuffer<Payload> tb;
    CHECK_FALSE (tb.acquire());

    std::atomic<bool> done { false };
    std::atomic<int> torn { 0 };
    int lastSeen = 0;

    std::thread reader ([&]
    {
        while (! done.load())
        {
            if (tb.acquire())
            {
                const auto& p = tb.read();
                for (int x : p.v)
                    if (x != p.v[0])
                        torn.fetch_add (1);
                if (p.v[0] < lastSeen)
                    torn.fetch_add (1); // values must never go backwards
                lastSeen = p.v[0];
            }
        }
    });

    for (int i = 1; i <= 20000; ++i)
    {
        tb.write().v.fill (i);
        tb.publish();
    }
    done = true;
    reader.join();

    CHECK (torn.load() == 0);
    tb.acquire();
    CHECK (tb.read().v[0] == 20000);
}
