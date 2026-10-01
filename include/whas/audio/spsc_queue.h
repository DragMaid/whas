#pragma once
#include <array>
#include <atomic>
#include <cstddef>

// Lock-free ring for one producer thread and one consumer thread. Push
// fails (drops) when full; for sound that's the right kind of throttling.
template <typename T, size_t Capacity> class SpscQueue {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity: power of two");

public:
  bool Push(const T &item) {
    size_t head = m_head.load(std::memory_order_relaxed);
    size_t next = (head + 1) & (Capacity - 1);
    if (next == m_tail.load(std::memory_order_acquire))
      return false;
    m_items[head] = item;
    m_head.store(next, std::memory_order_release);
    return true;
  }

  bool Pop(T &out) {
    size_t tail = m_tail.load(std::memory_order_relaxed);
    if (tail == m_head.load(std::memory_order_acquire))
      return false;
    out = m_items[tail];
    m_tail.store((tail + 1) & (Capacity - 1), std::memory_order_release);
    return true;
  }

private:
  std::array<T, Capacity> m_items{};
  alignas(64) std::atomic<size_t> m_head{0};
  alignas(64) std::atomic<size_t> m_tail{0};
};
