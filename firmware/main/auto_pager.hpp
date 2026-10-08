#pragma once
#include <cstddef>
#include <cstdint>

namespace rtd {
class AutoPager {
public:
  static constexpr int64_t Interval = 5000000; // monotonic microseconds

  // Start the dwell after the complete page has reached the screen.
  void shown(int64_t now) { last_shown = now; }

  bool advance(size_t &page, size_t count, int64_t now, bool pause) {
    if (pause) {
      was_paused = true;
      return false;
    }
    if (was_paused) {
      was_paused = false;
      shown(now);
      return false;
    }
    if (count <= 1 || now - last_shown < Interval)
      return false;
    page = (page + 1) % count;
    shown(now); // Never catch up by skipping pages after a delayed loop.
    return true;
  }

private:
  int64_t last_shown = 0;
  bool was_paused = false;
};
} // namespace rtd
