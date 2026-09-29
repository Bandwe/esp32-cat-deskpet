// Host-only R15 contact tests. No LCD, touch controller, serial port or flash access.
// The fifth down() argument requests an immediate visual response from the caller
// while deferring the semantic Tap until release, so a stroke is not counted as a tap.
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "../../sketch/DeskPetV01/TouchGesture.h"

using Action = TouchGesture::Action;

void expect(Action actual, Action wanted, const char* message) {
  if (actual != wanted) throw std::runtime_error(message);
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct Suite {
  unsigned passed = 0;
  unsigned failed = 0;

  template <typename Test>
  void run(const char* name, Test test) {
    try {
      test();
      ++passed;
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
      ++failed;
      std::cout << "FAIL " << name << ": " << error.what() << '\n';
    }
  }
};

int main() {
  Suite suite;

  suite.run("ordinary deferred contact taps once on release", [] {
    TouchGesture gesture;
    expect(gesture.down(120, 140, 100, false, true), Action::None,
           "deferred press became semantic Tap");
    expect(gesture.down(120, 140, 112, false, true), Action::None,
           "duplicate down emitted Tap");
    expect(gesture.move(126, 145, 130), Action::None,
           "sub-slop motion emitted Tap");
    expect(gesture.release(210), Action::Tap, "quick release missed Tap");
    require(gesture.x() == 126 && gesture.y() == 145,
            "release lost final hit-test coordinates");
    expect(gesture.release(220), Action::None, "release repeated Tap");
    require(!gesture.active(), "contact remained active");
  });

  suite.run("legacy ordinary contact remains immediate by default", [] {
    TouchGesture gesture;
    expect(gesture.down(120, 140, 0), Action::Tap,
           "existing immediate down behavior changed");
    expect(gesture.release(80), Action::None, "legacy contact tapped twice");
  });

  suite.run("deferred stroke never turns into a tap when returned to origin", [] {
    TouchGesture gesture;
    expect(gesture.down(120, 140, 0, false, true), Action::None,
           "stroke started as Tap");
    expect(gesture.move(133, 140, 50), Action::None,
           "drag emitted semantic Tap");
    expect(gesture.move(120, 140, 100), Action::None,
           "return to origin emitted Tap");
    expect(gesture.tick(2000), Action::None, "moving stroke slept the pet");
    expect(gesture.release(2050), Action::None,
           "returned stroke became release Tap");
  });

  suite.run("deferred tap slop is radial and inclusive", [] {
    TouchGesture gesture;
    gesture.down(120, 140, 0, false, true);
    gesture.move(132, 140, 50);
    expect(gesture.release(100), Action::Tap, "12px move was rejected");
    gesture.down(120, 140, 200, false, true);
    gesture.move(129, 149, 250);
    expect(gesture.release(300), Action::None,
           "diagonal distance greater than 12px was accepted");
  });

  suite.run("stationary deferred two-second hold sleeps once, not taps", [] {
    TouchGesture gesture;
    gesture.down(120, 140, 0, false, true);
    expect(gesture.tick(1999), Action::None, "hold fired before 2000ms");
    expect(gesture.tick(2000), Action::HoldSleep, "hold missed deadline");
    expect(gesture.tick(2100), Action::None, "hold repeated");
    expect(gesture.release(2200), Action::None, "hold became Tap");
    gesture.down(120, 140, 3000, false, true);
    expect(gesture.release(5000), Action::None,
           "release at hold deadline became Tap when tick was skipped");
  });

  suite.run("edge swipe opens once and cannot select menu on same contact", [] {
    TouchGesture gesture;
    expect(gesture.down(100, 36, 0, false, true), Action::None,
           "edge down emitted Tap");
    expect(gesture.move(140, 100, 1000), Action::OpenMenu,
           "inclusive 36/40/64/1000 boundary missed menu");
    expect(gesture.move(140, 170, 1200), Action::None, "menu reopened");
    expect(gesture.tick(2200), Action::None, "opening swipe slept the pet");
    expect(gesture.release(2250), Action::None,
           "opening swipe selected underlying menu item");
    gesture.down(120, 160, 2300, true, true);
    expect(gesture.release(2400), Action::Tap,
           "fresh menu tap after consumed swipe was lost");
  });

  suite.run("cat face at top disables edge swipe but keeps pet contact", [] {
    TouchGesture gesture;
    // The caller has already hit-tested this point against the displaced cat.
    expect(gesture.down(120, 20, 0, false, true, false), Action::None,
           "top-of-screen cat touch became an immediate Tap");
    expect(gesture.move(120, 84, 100), Action::None,
           "dragging a top-of-screen cat face opened the menu");
    expect(gesture.release(150), Action::None,
           "cat face drag became a semantic Tap");
    gesture.down(120, 20, 200, false, true, false);
    expect(gesture.release(350), Action::Tap,
           "stationary top-of-screen cat face lost ordinary Tap");
    gesture.down(120, 20, 400, false, true, false);
    expect(gesture.tick(2400), Action::HoldSleep,
           "disabling edge swipe also disabled hold-to-sleep");
    expect(gesture.release(2450), Action::None,
           "top-of-screen cat hold became Tap");
  });

  suite.run("empty top edge still opens menu when edge swipe is allowed", [] {
    TouchGesture gesture;
    expect(gesture.down(10, 20, 0, false, true, true), Action::None,
           "empty edge touched the pet immediately");
    expect(gesture.move(10, 84, 200), Action::OpenMenu,
           "empty top edge no longer opens the menu");
    expect(gesture.release(250), Action::None,
           "opening empty-edge swipe also tapped the pet");
  });

  suite.run("off-edge and ineligible drags do not open menu or tap", [] {
    TouchGesture gesture;
    gesture.down(100, 37, 0, false, true);
    expect(gesture.move(100, 101, 200), Action::None,
           "ordinary drag opened menu");
    expect(gesture.release(250), Action::None,
           "ordinary drag became Tap");
    gesture.down(100, 20, 300, false, true);
    gesture.move(141, 30, 400);
    expect(gesture.move(100, 100, 500), Action::None,
           "horizontal detour regained swipe eligibility");
    expect(gesture.release(550), Action::None,
           "horizontal detour became Tap");
    gesture.down(100, 20, 600, false, true);
    expect(gesture.move(100, 84, 1601), Action::None,
           "late edge swipe opened menu");
    expect(gesture.release(1700), Action::None,
           "late edge drag became Tap");
  });

  suite.run("stationary edge tap and hold retain their meanings", [] {
    TouchGesture gesture;
    gesture.down(100, 10, 0, false, true);
    expect(gesture.release(180), Action::Tap, "edge quick Tap was lost");
    gesture.down(100, 10, 300, false, true);
    expect(gesture.tick(2300), Action::HoldSleep,
           "stationary edge hold did not sleep");
    expect(gesture.release(2350), Action::None,
           "stationary edge hold also tapped");
  });

  suite.run("menu contact selects only on short stationary release", [] {
    TouchGesture gesture;
    expect(gesture.down(120, 160, 0, true, true), Action::None,
           "menu selected on down");
    expect(gesture.tick(2000), Action::None, "menu hold slept the pet");
    expect(gesture.release(2100), Action::None,
           "long menu press selected item");
    gesture.down(120, 160, 2200, true, true);
    gesture.move(140, 160, 2250);
    gesture.move(120, 160, 2300);
    expect(gesture.release(2350), Action::None,
           "menu drag returning to origin selected item");
    gesture.down(120, 160, 2400, true, true);
    expect(gesture.release(3400), Action::Tap,
           "1000ms menu tap deadline was rejected");
    gesture.down(120, 160, 3500, true, true);
    expect(gesture.release(4501), Action::None,
           "1001ms menu tap selected item");
  });

  suite.run("cancel and stale release cannot emit a deferred tap", [] {
    TouchGesture gesture;
    gesture.down(120, 140, 0, false, true);
    gesture.cancel();
    expect(gesture.release(180), Action::None,
           "canceled contact emitted Tap");
    expect(gesture.move(120, 220, 190), Action::None,
           "canceled contact emitted OpenMenu");
    expect(gesture.down(120, 140, 200, false, true), Action::None,
           "new contact did not start cleanly");
    expect(gesture.release(380), Action::Tap,
           "stale-contact fallback release lost legitimate quick tap");
  });

  suite.run("unsigned clock wrap preserves deferred tap, swipe and hold", [] {
    TouchGesture gesture;
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 500U;
    gesture.down(120, 140, start, false, true);
    expect(gesture.release(start + 180U), Action::Tap,
           "wrapped deferred Tap failed");
    gesture.down(100, 20, start, false, true);
    expect(gesture.move(100, 84, start + 200U), Action::OpenMenu,
           "wrapped edge swipe failed");
    expect(gesture.release(start + 220U), Action::None,
           "wrapped swipe became Tap");
    gesture.down(120, 140, start, false, true);
    expect(gesture.tick(start + 1999U), Action::None,
           "wrapped hold fired early");
    expect(gesture.tick(start + 2000U), Action::HoldSleep,
           "wrapped hold missed deadline");
    expect(gesture.release(start + 2100U), Action::None,
           "wrapped hold became Tap");
  });

  std::cout << "NOTE Synthetic recognizer tests only; button hit testing, physical touch feel, visual timing and LCD updates need separate checks.\n";
  std::cout << "SUMMARY passed=" << suite.passed << " failed=" << suite.failed << '\n';
  return suite.failed ? 1 : 0;
}
