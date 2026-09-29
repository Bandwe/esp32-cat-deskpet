// Host-only synthetic regression tests. This does not read a serial port or
// model the sensor/gravity filter, and is not a physical gesture acceptance test.
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using std::isfinite;  // Expose the C math spelling on both MSVC and ESP compilers.
#include "../../sketch/DeskPetV01/ShakeDetector.h"

namespace {
struct Vector { float x, y, z; };
Vector scaled(Vector value, float scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}
Vector unit(Vector value) {
  const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  return scaled(value, 1.0f / length);
}
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct Input {
  ShakeDetector detector{0.32f};
  uint32_t origin = 1000;
  unsigned triggers = 0;

  bool sample(Vector value, uint32_t elapsed) {
    const bool triggered = detector.update(value.x, value.y, value.z, origin + elapsed);
    if (triggered) ++triggers;
    return triggered;
  }
  bool pair(Vector value, uint32_t elapsed) {
    const bool first = sample(value, elapsed);
    const bool second = sample(value, elapsed + 20);
    return first || second;
  }
};

struct Suite {
  unsigned passed = 0, failed = 0;
  template<typename Function> void run(const char* name, Function test) {
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

void validShake(Vector direction, uint32_t origin = 1000) {
  Input input;
  input.origin = origin;
  const Vector positive = scaled(unit(direction), .55f);
  const Vector negative = scaled(positive, -1);
  require(!input.pair(positive, 0), "initial excursion triggered");
  require(input.detector.reversals() == 0, "initial excursion counted as reversal");
  require(!input.pair(negative, 160), "triggered after one reversal");
  require(input.detector.reversals() == 1, "first reversal missing");
  require(!input.pair(positive, 320), "triggered after two reversals");
  require(input.detector.reversals() == 2, "second reversal missing");
  require(!input.sample(negative, 480), "third reversal accepted from one sample");
  require(input.sample(negative, 500), "four confirmed excursions did not trigger");
  require(input.triggers == 1, "incorrect trigger count");
  require(input.detector.reversals() == 0, "trigger did not reset sequence");
  require(!input.sample(negative, 520), "same excursion retriggered");
}
}  // namespace

int main(int argc, char** argv) {
  Suite suite;
  const Vector positive{.55f, 0, 0}, negative{-.55f, 0, 0}, quiet{0, 0, 0};
  std::cout << "R7 SHAKE DETECTOR HOST SYNTHETIC REGRESSION\n"
            << "Input is post-gravity-filter acceleration in g; no hardware or serial access.\n";

  suite.run("shake positive X and exactly three confirmed reversals", [] { validShake({1, 0, 0}); });
  suite.run("shake negative X", [] { validShake({-1, 0, 0}); });
  suite.run("shake positive Y", [] { validShake({0, 1, 0}); });
  suite.run("shake negative Y", [] { validShake({0, -1, 0}); });
  suite.run("shake positive Z", [] { validShake({0, 0, 1}); });
  suite.run("shake negative Z", [] { validShake({0, 0, -1}); });
  suite.run("shake diagonal XYZ", [] { validShake({1, 1, 1}); });
  suite.run("shake oblique mixed-sign axis", [] { validShake({2, -3, 1}); });

  suite.run("single pickup and put-down do not trigger", [&] {
    Input input;
    input.pair(positive, 0);
    input.pair(negative, 180);
    for (uint32_t time = 220; time <= 1500; time += 20) input.sample(quiet, time);
    require(input.triggers == 0 && input.detector.reversals() == 0, "single movement triggered or retained stale state");
  });
  suite.run("low-amplitude alternating tilt does not trigger", [&] {
    Input input;
    for (uint32_t time = 0; time < 5000; time += 20)
      input.sample({(time / 160) % 2 ? -.31f : .31f, 0, 0}, time);
    require(input.triggers == 0, "subthreshold tilt triggered");
  });
  suite.run("sustained single direction does not trigger", [&] {
    Input input;
    for (uint32_t time = 0; time < 5000; time += 20) input.sample(positive, time);
    require(input.triggers == 0 && input.detector.reversals() == 0, "same direction became reversals");
  });
  suite.run("isolated alternating one-frame spikes cannot prime", [&] {
    Input input;
    for (uint32_t time = 0; time < 3000; time += 160) {
      input.sample((time / 160) % 2 ? negative : positive, time);
      input.sample(quiet, time + 20);
    }
    require(input.triggers == 0 && input.detector.reversals() == 0, "single-frame spikes formed a sequence");
  });
  suite.run("one-frame opposite spike cannot confirm a reversal", [&] {
    Input input;
    input.pair(positive, 0);
    input.sample(negative, 160);
    input.sample(quiet, 180);
    require(input.detector.reversals() == 0, "one opposite sample counted");
    input.pair(negative, 220);
    require(input.detector.reversals() == 1, "valid pair after rejected spike was not accepted");
  });
  suite.run("cross-axis tour does not count as an alternating-axis shake", [&] {
    Input input;
    const Vector tour[] = {{.55f,0,0}, {0,.55f,0}, {-.55f,0,0}, {0,-.55f,0},
                           {0,0,.55f}, {0,0,-.55f}, {.55f,0,0}, {0,.55f,0}};
    uint32_t time = 0;
    for (Vector direction : tour) { input.pair(direction, time); time += 160; }
    require(input.triggers == 0, "unrelated axes triggered");
  });
  suite.run("opposite cone rejects insufficient axis projection", [&] {
    Input input;
    input.pair(positive, 0);
    const Vector outsideCone = scaled(unit({-.64f, .7683749f, 0}), .55f);
    input.pair(outsideCone, 160);
    require(input.detector.reversals() == 0, "projection below 0.65 accepted");
  });
  suite.run("opposite cone accepts sufficient axis projection", [&] {
    Input input;
    input.pair(positive, 0);
    const Vector insideCone = scaled(unit({-.66f, .7512656f, 0}), .55f);
    input.pair(insideCone, 160);
    require(input.detector.reversals() == 1, "projection above 0.65 rejected");
  });
  suite.run("candidate gap over 80ms cannot confirm initial direction", [&] {
    Input input;
    input.sample(positive, 0);
    input.sample(positive, 81);
    input.pair(negative, 181);
    require(input.detector.reversals() == 0, "gapped samples incorrectly primed the positive direction");
  });
  suite.run("candidate gap over 80ms cannot confirm reversal", [&] {
    Input input;
    input.pair(positive, 0);
    input.sample(negative, 160);
    input.sample(negative, 241);
    require(input.detector.reversals() == 0, "gapped opposite pair counted");
    input.sample(negative, 261);
    require(input.detector.reversals() == 1, "fresh continuous pair after gap failed");
  });
  suite.run("candidate gap exactly 80ms remains valid", [&] {
    Input input;
    input.sample(positive, 0);
    input.sample(positive, 80);
    input.sample(negative, 180);
    input.sample(negative, 260);
    require(input.detector.reversals() == 1, "inclusive 80ms confirmation boundary failed");
  });
  suite.run("two samples less than 20ms apart cannot prime", [&] {
    Input input;
    input.sample(positive, 0);
    input.sample(positive, 10);
    input.pair(negative, 110);
    require(input.detector.reversals() == 0, "10ms span was accepted");
  });
  suite.run("100ms reversal guard rejects early opposite pair", [&] {
    Input input;
    input.pair(positive, 0);
    input.pair(negative, 60);
    require(input.detector.reversals() == 0, "early reversal counted");
    input.pair(negative, 120);
    require(input.detector.reversals() == 1, "valid opposite pair after guard failed");
  });
  suite.run("confirmed intervals exactly 450ms remain valid", [&] {
    Input input;
    input.pair(positive, 0);
    input.pair(negative, 450);
    input.pair(positive, 900);
    require(input.pair(negative, 1350), "three inclusive 450ms intervals did not trigger");
    require(input.triggers == 1, "unexpected trigger count at maximum cadence interval");
  });
  suite.run("451ms interval expires the previous sequence", [&] {
    Input input;
    input.pair(positive, 0);
    input.pair(negative, 471);
    require(input.detector.reversals() == 0 && input.triggers == 0, "expired first excursion persisted");
  });
  suite.run("expiry during pending confirmation clears candidate", [&] {
    Input input;
    input.pair(positive, 0);
    input.sample(negative, 450);
    input.sample(negative, 471);
    require(input.detector.reversals() == 0, "confirmation crossed the 450ms sequence timeout");
    input.pair(positive, 591);
    require(input.detector.reversals() == 0, "expired candidate became a confirmed initial direction");
  });
  suite.run("elapsed over 1400ms cannot join an old sequence", [&] {
    Input input;
    input.pair(positive, 0);
    input.pair(negative, 430);
    input.pair(positive, 860);
    require(input.detector.reversals() == 2, "timeout setup failed");
    input.pair(negative, 1421);
    require(input.triggers == 0 && input.detector.reversals() == 0, "stale sequence triggered");
    // This also exceeds the 450ms last-turn timeout. With only three required
    // reversals, 3 * 450 = 1350ms, so the 1400ms guard cannot independently be
    // reached through the public API while every turn interval stays valid.
  });
  suite.run("NaN Inf overflow invalidate pending confirmations", [&] {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float big = std::numeric_limits<float>::max();
    const Vector invalids[] = {{nan,0,0}, {0,nan,0}, {0,0,nan}, {inf,0,0},
                              {0,-inf,0}, {0,0,inf}, {big,big,big}};
    for (Vector invalid : invalids) {
      Input input;
      input.pair(positive, 0);
      input.sample(negative, 120);
      require(!input.sample(invalid, 140), "nonfinite sample triggered");
      input.sample(negative, 160);
      require(input.detector.reversals() == 0, "invalid sample did not clear pending direction");
      input.sample(negative, 180);
      require(input.detector.reversals() == 1, "valid fresh pair after nonfinite input failed");
    }
  });
  suite.run("explicit reset clears confirmed sequence", [&] {
    Input input;
    input.pair(positive, 0);
    input.pair(negative, 160);
    input.pair(positive, 320);
    require(input.detector.reversals() == 2, "reset setup failed");
    input.detector.reset();
    require(input.detector.reversals() == 0, "reset left a reversal count");
    input.pair(negative, 480);
    input.pair(positive, 640);
    input.pair(negative, 800);
    require(input.triggers == 0 && input.detector.reversals() == 2, "old sequence survived reset");
    require(input.pair(positive, 960), "new sequence failed after reset");
  });
  suite.run("explicit reset clears pending initial candidate", [&] {
    Input input;
    input.sample(positive, 0);
    input.detector.reset();
    input.sample(positive, 20);
    input.pair(negative, 120);
    require(input.detector.reversals() == 0, "pending initial candidate survived reset");
  });
  suite.run("uint32 timestamp wrap retains normal cadence", [] {
    validShake({1, -2, 3}, std::numeric_limits<uint32_t>::max() - 50U);
  });
  suite.run("uint32 timestamp wrap still enforces timeout", [&] {
    Input input;
    input.origin = std::numeric_limits<uint32_t>::max() - 50U;
    input.pair(positive, 0);
    input.pair(negative, 471);
    require(input.triggers == 0 && input.detector.reversals() == 0, "wrapped timeout failed");
  });

  suite.run("main sketch has linear-only trigger and unchanged R6 display mapping", [&] {
    require(argc == 2, "pass the main sketch path as the single argument");
    std::ifstream stream(argv[1], std::ios::binary);
    require(stream.good(), "cannot open main sketch for static read");
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    require(text.find("angularTriggered") == std::string::npos, "angularTriggered remains");
    require(text.find("angularShake") == std::string::npos, "angularShake remains");
    require(text.find("ShakeDetector linearShake(0.32f)") != std::string::npos, "linear threshold differs from R7 proposal");
    require(text.find("if (linearShake.update(dx, dy, dz, now)) triggerDizzy(now, true);") != std::string::npos,
            "expected direct linear-only physical trigger is missing");
    require(text.find("const float screenX = -dy + (gyroRead ? gx * .002f : 0);") != std::string::npos,
            "screenX differs from R6 mapping");
    require(text.find("const float screenY = dx + (gyroRead ? gy * .002f : 0);") != std::string::npos,
            "screenY differs from R6 mapping");
    require(text.find("face.observeMotion(screenX, screenY, gyroRead ? -gz : 0, now);") != std::string::npos,
            "R6 tilt input differs");
  });

  std::cout << "NOTE 1400ms guard cannot be independently exercised with three reversals "
               "and all confirmed intervals <=450ms (maximum total 1350ms).\n"
            << "NOTE These are synthetic detector and static-source checks, not physical sensor/gesture tests.\n"
            << "SUMMARY passed=" << suite.passed << " failed=" << suite.failed << '\n';
  return suite.failed ? 1 : 0;
}
