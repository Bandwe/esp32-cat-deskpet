// Host-only synthetic contact tests; no firmware/serial or visual acceptance.
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include "../../sketch/DeskPetV01/TouchGesture.h"

using Action = TouchGesture::Action;
void expect(Action actual, Action expected, const char* note) {
  if (actual != expected) throw std::runtime_error(note);
}
void require(bool condition, const char* note) {
  if (!condition) throw std::runtime_error(note);
}
struct Suite {
  unsigned passed = 0, failed = 0;
  template<typename Test> void run(const char* name, Test test) {
    try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& error) {
      ++failed; std::cout << "FAIL " << name << ": " << error.what() << '\n';
    }
  }
};

int main() {
  Suite suite;
  suite.run("ordinary tap is immediate and never repeats on release", [] {
    TouchGesture gesture;
    expect(gesture.down(100, 100, 0), Action::Tap, "missing immediate Tap");
    expect(gesture.down(100, 100, 12), Action::None, "duplicate down restarted contact");
    expect(gesture.release(100), Action::None, "release repeated Tap");
    require(!gesture.active(), "contact still active");
  });
  suite.run("top-edge stationary tap is deferred until release", [] {
    TouchGesture gesture;
    expect(gesture.down(100, 20, 0), Action::None, "edge contact tapped immediately");
    expect(gesture.move(103, 24, 24), Action::None, "slop became a gesture");
    expect(gesture.release(200), Action::Tap, "edge Tap missing");
    require(gesture.x() == 103 && gesture.y() == 24, "release lost hit-test position");
  });
  suite.run("swipe emits once and consumes contact until a fresh menu tap", [] {
    TouchGesture gesture;
    gesture.down(100, 20, 0);
    expect(gesture.move(110, 84, 300), Action::OpenMenu, "swipe did not open");
    expect(gesture.move(120, 160, 600), Action::None, "swipe repeated");
    expect(gesture.tick(2500), Action::None, "opened menu contact also held asleep");
    expect(gesture.release(2600), Action::None, "opening swipe selected an item");
    expect(gesture.down(120, 160, 2800, true), Action::None, "menu item tapped on down");
    expect(gesture.release(3000), Action::Tap, "fresh menu click missing");
  });
  suite.run("edge and horizontal thresholds use inclusive boundaries", [] {
    TouchGesture gesture;
    gesture.down(100, 36, 0);
    expect(gesture.move(140, 100, 100), Action::OpenMenu, "36/40/64 boundary rejected");
    gesture.release(120);
    expect(gesture.down(100, 37, 200), Action::Tap, "non-edge down unexpectedly deferred");
    expect(gesture.move(100, 101, 300), Action::None, "non-edge swipe opened menu");
    gesture.release(320);
    gesture.down(100, 20, 400);
    gesture.move(141, 40, 450);
    expect(gesture.move(100, 100, 500), Action::None, "horizontal detour regained eligibility");
    expect(gesture.release(520), Action::None, "rejected drag became Tap");
  });
  suite.run("swipe accepts 1000ms but rejects 1001ms", [] {
    TouchGesture gesture;
    gesture.down(100, 0, 0);
    expect(gesture.move(100, 64, 1000), Action::OpenMenu, "deadline boundary rejected");
    gesture.release(1010);
    gesture.down(100, 0, 2000);
    expect(gesture.move(100, 64, 3001), Action::None, "late swipe opened menu");
    expect(gesture.release(3100), Action::None, "late drag became Tap");
  });
  suite.run("partial edge drag cannot tap or hold asleep", [] {
    TouchGesture gesture;
    gesture.down(100, 10, 0);
    gesture.move(100, 40, 100);
    gesture.move(100, 10, 200);
    expect(gesture.tick(2100), Action::None, "returned drag held asleep");
    expect(gesture.release(2200), Action::None, "returned drag became Tap");
  });
  suite.run("ordinary two-second stationary hold fires once", [] {
    TouchGesture gesture;
    gesture.down(100, 100, 0);
    expect(gesture.tick(1999), Action::None, "hold fired early");
    expect(gesture.tick(2000), Action::HoldSleep, "hold missing at deadline");
    expect(gesture.move(100, 100, 2012), Action::None, "move repeated hold");
    expect(gesture.tick(3000), Action::None, "tick repeated hold");
    expect(gesture.release(3100), Action::None, "hold became Tap");
  });
  suite.run("stationary edge contact retains two-second sleep hold", [] {
    TouchGesture gesture;
    gesture.down(100, 10, 0);
    expect(gesture.tick(1001), Action::None, "edge timeout emitted an action");
    expect(gesture.tick(2000), Action::HoldSleep, "edge hold lost normal sleep behavior");
    expect(gesture.release(2100), Action::None, "edge hold also tapped");
  });
  suite.run("menu drag cannot select even after returning to its origin", [] {
    TouchGesture gesture;
    gesture.down(120, 160, 0, true);
    gesture.move(140, 160, 100);
    gesture.move(120, 160, 200);
    expect(gesture.release(300), Action::None, "menu drag selected item");
    gesture.down(100, 10, 400, true);
    expect(gesture.move(100, 100, 500), Action::None, "menu contact reopened menu");
    expect(gesture.tick(2500), Action::None, "menu drag held asleep");
  });
  suite.run("menu long press is not a selection; 12px slop remains a tap", [] {
    TouchGesture gesture;
    gesture.down(100, 100, 0, true);
    expect(gesture.tick(2100), Action::None, "menu long press slept");
    expect(gesture.release(2200), Action::None, "menu long press selected");
    gesture.down(100, 100, 2300, true);
    gesture.move(112, 100, 2400);
    expect(gesture.release(2500), Action::Tap, "inclusive tap slop rejected");
    gesture.down(100, 100, 2600, true);
    gesture.move(109, 109, 2700);
    expect(gesture.release(2800), Action::None, "diagonal distance >12px selected");
  });
  suite.run("caller stale-release and cancel terminate contact state", [] {
    TouchGesture gesture;
    gesture.down(100, 20, 0);
    expect(gesture.release(180), Action::Tap, "caller stale-release lost edge tap");
    expect(gesture.tick(2200), Action::None, "released contact held asleep");
    gesture.down(100, 20, 2300);
    gesture.cancel();
    expect(gesture.release(2500), Action::None, "canceled contact released Tap");
    expect(gesture.move(100, 100, 2600), Action::None, "canceled contact opened menu");
    expect(gesture.down(100, 100, 2700), Action::Tap, "new contact after cancel failed");
  });
  suite.run("unsigned clock wrap preserves swipe and hold timing", [] {
    TouchGesture gesture;
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 100U;
    gesture.down(100, 20, start);
    expect(gesture.move(100, 84, start + 200U), Action::OpenMenu, "wrapped swipe failed");
    gesture.release(start + 220U);
    gesture.down(100, 100, start);
    expect(gesture.tick(start + 1999U), Action::None, "wrapped hold early");
    expect(gesture.tick(start + 2000U), Action::HoldSleep, "wrapped hold failed");
  });
  std::cout << "NOTE Synthetic contact-state tests only; physical touch feel remains unverified.\n"
            << "SUMMARY passed=" << suite.passed << " failed=" << suite.failed << '\n';
  return suite.failed ? 1 : 0;
}
