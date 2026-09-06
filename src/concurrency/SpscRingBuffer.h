#pragma once

#include <atomic>
#include <cstddef>
#include <array>
#include <utility>

namespace concurrency {

template <typename T, size_t Capacity>
class SpscRingBuffer {
    static_assert(Capacity > 0, "Capacity must be > 0");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 for fast modulo");

public:
    SpscRingBuffer() : head_(0), cached_tail_(0), tail_(0), cached_head_(0) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    template <typename U>
    bool push(U&& item) {
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t next_head = (current_head + 1) & mask_;

        if (next_head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (next_head == cached_tail_) {
                return false;
            }
        }

        buffer_[current_head] = std::forward<U>(item);
        
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);

        if (current_tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail == cached_head_) {
                return false;
            }
        }

        item = std::move(buffer_[current_tail]);
        size_t next_tail = (current_tail + 1) & mask_;
        
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t mask_ = Capacity - 1;

    alignas(64) std::atomic<size_t> head_;
    size_t cached_tail_;

    alignas(64) std::atomic<size_t> tail_;
    size_t cached_head_;

    alignas(64) std::array<T, Capacity> buffer_;
};

}