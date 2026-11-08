#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

// A small iterative radix-2 FFT (no JUCE dependency), for the onset detector.
// Runs on the message thread after a take, never on the audio thread, so it is
// plain rather than fast.
namespace evaluation
{
[[nodiscard]] constexpr bool isPowerOfTwo(std::size_t value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

// In place, forward. `data.size()` must be a power of two.
inline void fft(std::vector<std::complex<float>>& data)
{
    const std::size_t size = data.size();
    if (size < 2 || !isPowerOfTwo(size))
    {
        return;
    }

    for (std::size_t index = 1, reversed = 0; index < size; ++index)
    {
        std::size_t bit = size >> 1U;
        for (; (reversed & bit) != 0; bit >>= 1U)
        {
            reversed ^= bit;
        }
        reversed ^= bit;
        if (index < reversed)
        {
            std::swap(data[index], data[reversed]);
        }
    }

    constexpr double twoPi = 6.283185307179586;
    for (std::size_t length = 2; length <= size; length <<= 1U)
    {
        const double angle = -twoPi / static_cast<double>(length);
        const std::complex<float> step(
            static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        for (std::size_t start = 0; start < size; start += length)
        {
            std::complex<float> twiddle(1.0f, 0.0f);
            for (std::size_t offset = 0; offset < length / 2; ++offset)
            {
                const std::complex<float> even = data[start + offset];
                const std::complex<float> odd = data[start + offset + (length / 2)] * twiddle;
                data[start + offset] = even + odd;
                data[start + offset + (length / 2)] = even - odd;
                twiddle *= step;
            }
        }
    }
}
} // namespace evaluation
