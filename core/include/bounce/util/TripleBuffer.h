#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace bounce::util
{

/** Lock-free single-producer / single-consumer "latest value" exchange.

    The message thread writes a complete new T with write() + publish(); the audio thread calls
    acquire() at the top of each block and reads the newest published value. Neither side ever
    blocks or allocates (T should be a fixed-capacity POD-ish type), and the reader never sees a
    half-written value. Intermediate values may be skipped, which is fine for "current loop" data. */
template <typename T>
class TripleBuffer
{
public:
    TripleBuffer() = default;

    /** Producer: the buffer to fill. Stays valid until publish(). */
    T& write() noexcept { return buffers[writeIndex]; }

    /** Producer: makes the written buffer the newest one. */
    void publish() noexcept
    {
        const uint8_t prev = middle.exchange (static_cast<uint8_t> (writeIndex | dirtyBit), std::memory_order_acq_rel);
        writeIndex = prev & indexMask;
    }

    /** Consumer: switches to the newest published buffer if there is one.
        Returns true when the value changed since the last call. */
    bool acquire() noexcept
    {
        if ((middle.load (std::memory_order_relaxed) & dirtyBit) == 0)
            return false;
        const uint8_t prev = middle.exchange (readIndex, std::memory_order_acq_rel);
        readIndex = prev & indexMask;
        return true;
    }

    /** Consumer: the current value (as of the last acquire()). */
    const T& read() const noexcept { return buffers[readIndex]; }

private:
    static constexpr uint8_t dirtyBit = 0x4;
    static constexpr uint8_t indexMask = 0x3;

    std::array<T, 3> buffers {};
    uint8_t writeIndex = 0;          // producer only
    uint8_t readIndex = 1;           // consumer only
    std::atomic<uint8_t> middle { 2 };
};

} // namespace bounce::util
