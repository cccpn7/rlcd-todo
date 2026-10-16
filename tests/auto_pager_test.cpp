#include "../firmware/main/auto_pager.hpp"
#include <cassert>
#include <iostream>

int main() {
  using rtd::AutoPager;
  AutoPager timer;
  size_t page = 0;
  timer.shown(100);
  assert(!timer.advance(page, 4, 5000099, false));
  assert(timer.advance(page, 4, 5000100, false) && page == 1);
  // Four actual pages: focus, misc 1/2, misc 2/2, follow (possibly empty).
  int64_t now = 5000100;
  for (size_t expected : {2u, 3u, 0u}) {
    now += AutoPager::Interval;
    assert(timer.advance(page, 4, now, false) && page == expected);
  }
  // A slow refresh still leaves the newly displayed page visible for five seconds.
  timer.shown(21000000);
  assert(!timer.advance(page, 4, 25999999, false));
  assert(timer.advance(page, 4, 26000000, false));
  // Manual paging, long-press home, and new publications all call shown().
  for (size_t destination : {3u, 0u, 0u}) {
    page = destination;
    timer.shown(30000000);
    assert(!timer.advance(page, 4, 34999999, false));
    assert(page == destination);
    assert(timer.advance(page, 4, 35000000, false));
    assert(page == (destination + 1) % 4);
  }
  // Pairing or a held button pauses, then provides a full new dwell on release.
  assert(!timer.advance(page, 4, 40000000, true));
  size_t held_page = page;
  assert(!timer.advance(page, 4, 140000000, true) && page == held_page);
  assert(!timer.advance(page, 4, 140000001, false));
  assert(!timer.advance(page, 4, 145000000, false));
  assert(timer.advance(page, 4, 145000001, false));
  // No burst of catch-up flips after a stall; no flips on the welcome screen.
  held_page = page;
  assert(timer.advance(page, 4, 900000000, false));
  assert(page == (held_page + 1) % 4);
  assert(!timer.advance(page, 4, 900000000, false));
  assert(!timer.advance(page, 0, 999000000, false));
  assert(!timer.advance(page, 1, 999000000, false));
  // A new publication with fewer pages starts at home and gets its full dwell.
  page = 0;
  timer.shown(1000000000);
  assert(!timer.advance(page, 3, 1004999999, false));
  assert(timer.advance(page, 3, 1005000000, false) && page == 1);
  std::cout << "Auto paging: timing, wrap, manual reset, pause, publication passed\n";
}
