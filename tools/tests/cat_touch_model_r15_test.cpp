// Pure host-side tests for the black-cat touch intent model. No Arduino,
// physical touch controller, LCD, serial connection, or flash access.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "../../sketch/DeskPetV01/CatTouchModel.h"

using Mode = CatTouchModel::Mode;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void near(float actual, float expected, float tolerance, const char* message) {
  if (std::fabs(actual - expected) > tolerance) throw std::runtime_error(message);
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

  suite.run("horizontal brow motion starts one stroke", [] {
    CatTouchModel touch;
    touch.down(120, 100, 120, 140, 10);
    require(touch.active() && touch.mode() == Mode::Look,
            "brow down should start as Look");
    require(touch.move(133, 100), "horizontal brow stroke was not recognized");
    require(touch.mode() == Mode::Stroke, "horizontal brow became Drag");
    require(!touch.move(146, 100), "stroke restarted on later movement");
    require(touch.strokeStrength() > 0, "stroke travel did not accumulate");
    require(touch.dragX() == 0 && touch.dragY() == 0,
            "stroke exposed face-drag offset");
  });

  suite.run("vertical brow motion is a drag, not a stroke", [] {
    CatTouchModel touch;
    touch.down(120, 100, 120, 140, 0);
    require(touch.move(120, 113), "vertical brow drag was not recognized");
    require(touch.mode() == Mode::Drag, "vertical brow became Stroke");
    require(touch.dragX() == 0 && touch.dragY() == 13,
            "vertical drag offset incorrect");
    near(touch.strokeStrength(), 0, 0.0001f,
         "vertical drag accumulated stroke strength");
  });

  suite.run("eyes drag horizontally and never become a brow stroke", [] {
    CatTouchModel touch;
    touch.down(120, 140, 120, 140, 0);
    require(touch.move(133, 140), "eye drag was not recognized");
    require(touch.mode() == Mode::Drag, "eye motion became Stroke");
    require(touch.dragX() == 13 && touch.dragY() == 0,
            "eye drag offset incorrect");
    require(!touch.move(160, 150), "drag restarted on later movement");
    require(touch.dragX() == 40 && touch.dragY() == 10,
            "continuing drag did not follow finger");
  });

  suite.run("face-outside contact only looks despite large movement", [] {
    CatTouchModel touch;
    touch.down(120, 210, 120, 140, 0);
    require(!touch.move(210, 80), "outside contact started petting or drag");
    require(touch.mode() == Mode::Look, "outside contact left Look mode");
    require(touch.dragX() == 0 && touch.dragY() == 0,
            "outside contact moved the face");
    touch.release(100);
    require(touch.releasedMode() == Mode::Look,
            "outside contact did not preserve released Look mode");
  });

  suite.run("hit zones follow current face center and include boundaries", [] {
    CatTouchModel touch;
    touch.down(206, 142, 120, 200, 0);  // x +86, y -58: brow edge.
    require(touch.move(219, 142) && touch.mode() == Mode::Stroke,
            "moving face brow boundary was missed");
    touch.release(100);
    touch.down(120, 180, 120, 200, 200);  // y -20: eyes, not brow.
    require(touch.move(133, 180) && touch.mode() == Mode::Drag,
            "eye/brow boundary classified as Stroke");
    touch.release(300);
    touch.down(207, 142, 120, 200, 400);  // x +87: outside.
    require(!touch.move(220, 142) && touch.mode() == Mode::Look,
            "outside pair boundary classified as brow");
  });

  suite.run("faceZone identifies a displaced cat at the top edge", [] {
    require(CatTouchModel::faceZone(120, 4, 120, 62),
            "visible brow at upper zone boundary was missed");
    require(CatTouchModel::faceZone(206, 36, 120, 62),
            "visible top-edge cat at horizontal boundary was missed");
    require(!CatTouchModel::faceZone(207, 36, 120, 62),
            "empty top edge outside horizontal boundary matched cat");
    require(!CatTouchModel::faceZone(120, 3, 120, 62),
            "empty top edge above brow matched cat");
    require(CatTouchModel::faceZone(120, 101, 120, 62),
            "lower eye-zone boundary was missed");
    require(!CatTouchModel::faceZone(120, 102, 120, 62),
            "outside below eye-zone matched cat");
  });

  suite.run("top-edge cat face remains strokeable or draggable", [] {
    CatTouchModel touch;
    // Face center has risen to y=62, putting its brow inside y<=36.
    touch.down(120, 20, 120, 62, 0);
    require(touch.move(140, 20) && touch.mode() == Mode::Stroke,
            "cat brow on top edge did not recognize horizontal petting");
    touch.release(100);
    // At y=30, the eye zone itself intersects the top-edge menu gesture area.
    touch.down(120, 20, 120, 30, 200);
    require(touch.move(140, 20) && touch.mode() == Mode::Drag,
            "cat eye on top edge did not recognize dragging");
  });

  suite.run("12px radial threshold is inclusive, 13px starts an intent", [] {
    CatTouchModel touch;
    touch.down(120, 100, 120, 140, 0);
    require(!touch.move(132, 100) && touch.mode() == Mode::Look,
            "12px axis movement crossed threshold");
    require(touch.move(133, 100) && touch.mode() == Mode::Stroke,
            "13px axis movement did not cross threshold");
    touch.release(100);
    touch.down(120, 100, 120, 140, 200);
    require(touch.move(129, 109) && touch.mode() == Mode::Drag,
            "9px+9px diagonal distance failed radial threshold");
  });

  suite.run("unmoved brow and eye contacts remain Look through release", [] {
    CatTouchModel touch;
    touch.down(120, 100, 120, 140, 0);
    touch.move(125, 105);
    require(touch.mode() == Mode::Look, "small brow move changed mode");
    touch.release(100);
    require(touch.releasedMode() == Mode::Look,
            "unmoved brow release changed mode");
    touch.down(120, 140, 120, 140, 200);
    touch.release(250);
    require(touch.releasedMode() == Mode::Look,
            "stationary eye release changed mode");
  });

  suite.run("stroking accumulates reversals and caps strength", [] {
    CatTouchModel touch;
    touch.down(120, 100, 120, 140, 0);
    touch.move(133, 100);
    near(touch.strokeStrength(), 13.0f / 95.0f, 0.0001f,
         "initial stroke travel incorrect");
    touch.move(120, 100);
    near(touch.strokeStrength(), 26.0f / 95.0f, 0.0001f,
         "reverse stroke travel did not accumulate");
    touch.move(240, 100);
    near(touch.strokeStrength(), 1, 0.0001f,
         "strong stroke did not cap at one");
    touch.release(300);
    near(touch.strokeStrength(), 1, 0.0001f,
         "release unexpectedly erased stroke history");
  });

  suite.run("release saves final mode, coordinates and times", [] {
    CatTouchModel touch;
    touch.down(120, 140, 120, 140, 123);
    touch.move(150, 160);
    touch.release(456);
    require(!touch.active() && touch.mode() == Mode::None,
            "release left a live intent");
    require(touch.releasedMode() == Mode::Drag,
            "release lost the last active mode");
    require(touch.x() == 150 && touch.y() == 160,
            "release lost final finger coordinates");
    require(touch.downAt() == 123 && touch.releaseAt() == 456,
            "release lost timestamps");
    require(touch.dragX() == 0 && touch.dragY() == 0,
            "released contact still applies drag offset");
    require(!touch.move(200, 200), "inactive movement started a new intent");
  });

  suite.run("cancel clears active intent and cannot be released again", [] {
    CatTouchModel touch;
    touch.down(120, 100, 120, 140, 0);
    touch.move(150, 100);
    touch.cancel();
    require(!touch.active() && touch.mode() == Mode::None,
            "cancel left active contact");
    require(touch.releasedMode() == Mode::None,
            "cancel retained released intent");
    near(touch.strokeStrength(), 0, 0.0001f,
         "cancel retained stroke strength");
    require(!touch.move(180, 100), "canceled contact accepted movement");
    touch.release(500);
    require(touch.releasedMode() == Mode::None,
            "canceled contact produced released mode");
  });

  suite.run("new contact clears the previous released mode", [] {
    CatTouchModel touch;
    touch.down(120, 140, 120, 140, 0);
    touch.move(140, 140);
    touch.release(100);
    require(touch.releasedMode() == Mode::Drag,
            "setup drag did not release");
    touch.down(120, 100, 120, 140, 200);
    require(touch.releasedMode() == Mode::None,
            "new contact retained stale released mode");
    near(touch.strokeStrength(), 0, 0.0001f,
         "new contact retained stale stroke travel");
  });

  suite.run("extreme uint16 coordinates have safe signed drag offsets", [] {
    CatTouchModel touch;
    const uint16_t maximum = std::numeric_limits<uint16_t>::max();
    touch.down(0, 0, 0, 0, 0);  // Eye zone.
    require(touch.move(maximum, maximum),
            "max coordinate move did not start drag");
    require(touch.mode() == Mode::Drag &&
            touch.dragX() == 65535 && touch.dragY() == 65535,
            "positive 16-bit coordinate delta overflowed");
    touch.release(100);
    touch.down(maximum, maximum, maximum, maximum, 200);
    require(touch.move(0, 0), "min coordinate move did not start drag");
    require(touch.mode() == Mode::Drag &&
            touch.dragX() == -65535 && touch.dragY() == -65535,
            "negative 16-bit coordinate delta overflowed");
  });

  std::cout << "NOTE Synthetic geometry checks only; physical touch sampling, LCD feedback and cat animation remain unverified.\n";
  std::cout << "SUMMARY passed=" << suite.passed << " failed=" << suite.failed << '\n';
  return suite.failed ? 1 : 0;
}
