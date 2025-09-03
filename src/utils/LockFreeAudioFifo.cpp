#pragma once
#include <atomic>
#include <cstddef>
#include <algorithm>

class LockFreeAudioFifo
{
public:
    explicit LockFreeAudioFifo(size_t capacitySamples)
        : capacity(nextPowerOfTwo(std::max<size_t>(1024, capacitySamples))),
          mask(capacity - 1),
          buffer(new float[capacity])
    {
        writeIndex.store(0);
        readIndex.store(0);
    }

    ~LockFreeAudioFifo() { delete[] buffer; }

    // SPSC: producer (audio thread)
    void push(const float* src, int count)
    {
        size_t w = writeIndex.load(std::memory_order_relaxed);
        size_t r = readIndex.load(std::memory_order_acquire);
        size_t freeSpace = capacity - (w - r);
        size_t toWrite = (size_t) std::min<int>(count, (int)(freeSpace));
        if (toWrite == 0) return;

        for (size_t i = 0; i < toWrite; ++i)
            buffer[(w + i) & mask] = src[i];

        writeIndex.store(w + toWrite, std::memory_order_release);
    }

    // SPSC: consumer (render thread)
    int pop(float* dst, int maxCount)
    {
        size_t r = readIndex.load(std::memory_order_relaxed);
        size_t w = writeIndex.load(std::memory_order_acquire);
        size_t available = w - r;
        size_t toRead = (size_t) std::min<int>(maxCount, (int) available);
        if (toRead == 0) return 0;

        for (size_t i = 0; i < toRead; ++i)
            dst[i] = buffer[(r + i) & mask];

        readIndex.store(r + toRead, std::memory_order_release);
        return (int) toRead;
    }

    void reset()
    {
        readIndex.store(0, std::memory_order_relaxed);
        writeIndex.store(0, std::memory_order_relaxed);
    }

private:
    static size_t nextPowerOfTwo(size_t v)
    {
        if (v == 0) return 1;
        --v;
        v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v |= v >> 32;
        return v + 1;
    }

    const size_t capacity;
    const size_t mask;
    float* buffer;
    std::atomic<size_t> writeIndex { 0 };
    std::atomic<size_t> readIndex  { 0 };
};
