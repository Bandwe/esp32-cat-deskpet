// DeskPet V01: standalone local expressions for Waveshare ESP32-S3-Touch-LCD-1.69.
// Pin assignments and peripheral setup follow the official Waveshare examples.
#include <Arduino.h>
#include <Wire.h>
#include <HWCDC.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include <SensorQMI8658.hpp>
#include <esp_system.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "PetFace.h"
#include "ShakeDetector.h"
#include "TouchGesture.h"
#include "SagePet.h"
#include "BlackCatPet.h"
#include "PetMenu.h"
#include "PcMonitorPage.h"
#include "PcMonitorProtocol.h"
#include "EspMessageService.h"
#include "EspMessagePage.h"

static constexpr char VERSION[] = "deskpet-v01-20260929-r22-touch-finite";
static constexpr int LCD_DC = 4, LCD_CS = 5, LCD_SCK = 6, LCD_MOSI = 7;
static constexpr int LCD_RST = 8, LCD_BL = 15, I2C_SDA = 11, I2C_SCL = 10;
static constexpr int TOUCH_RST = 13, TOUCH_INT = 14, SYS_EN = 41, BUZZER = 42;
static constexpr uint8_t TOUCH_ADDRESS = 0x15;
static constexpr uint32_t FRAME_MS = 33, DIZZY_MS = 6000;
static constexpr uint32_t TOUCH_RELEASE_MS = 180, SHAKE_COOLDOWN_MS = 8000;

HWCDC USBSerial;
Arduino_ESP32SPI lcdBus(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_ST7789 lcd(&lcdBus, LCD_RST, 0, true, 240, 280, 0, 20, 0, 20);
Arduino_Canvas canvas(240, 280, &lcd);
SensorQMI8658 imu;
PetFace face;
SagePet sage;
BlackCatPet blackCat;
TouchGesture gesture;
Preferences preferences;
uint8_t selectedSkin = 0;
bool menuOpen = false, menuDirty = false, fullRefresh = true;
bool monitorOpen = false, monitorDirty = false;
bool webPageOpen = false, webDirty = false, cardOpen = false, cardDirty = false;
enum class CardReturn : uint8_t { Pet, Menu, Monitor, Web };
CardReturn cardReturn = CardReturn::Pet;
EspMessageService::Snapshot messageSnapshot;
EspMessagePage::Ui webUi;
uint32_t lastMessageCheck = 0, lastWebDraw = 0, cardRevisionShown = 0;
uint64_t cardTokenId = 0;
PcMonitorPage::View monitorView = PcMonitorPage::View::Overview;
PcMonitorPage::Snapshot monitorSnapshot;
uint32_t monitorFrames = 0, monitorSamples = 0, monitorLastDraw = 0;
bool preferencesReady = false, selectionSaved = true;
uint32_t menuOpens = 0, skinSelections = 0;

volatile bool touchPending = false;
bool displayReady = false, touchReady = false, imuReady = false, sleeping = false;
bool pressed = false, wakeTouch = false, accelPrimed = false;
bool menuButtonContact = false, catContact = false;
uint32_t touchLastSample = 0, lastTouchPoll = 0;
uint32_t lastImuPoll = 0, moodDeadline = 0, nextIdle = 0, lastFrame = 0;
uint32_t lastStats = 0, lastTap = 0, lastShake = 0;
uint32_t frames = 0, statsFrames = 0, taps = 0, holds = 0, shakes = 0, imuSamples = 0;
uint32_t logDropped = 0, previousLoopAt = 0, loopGapMaxMs = 0;
int16_t previousDrawTop = 0, previousDrawBottom = 279;
BlackCatPet::Rect previousCatEyes = {0,0,-1,-1};
BlackCatPet::Rect previousCatTongue = {0,0,-1,-1};
char logBuffer[640] = {};
uint8_t tapStreak = 0;
ShakeDetector linearShake(0.32f);
float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
float gravityX = 0, gravityY = 0, gravityZ = 0, accelerationG = 0;
float motionPeakG = 0, gyroPeakDps = 0;
char serialLine[160] = {};
size_t serialLength = 0;
bool serialOverflow = false;

void ARDUINO_ISR_ATTR touchInterrupt() { touchPending = true; }

// Diagnostics must never wait for the desktop to read the USB port. There is
// only one writer (the main loop); the USB ISR can only free buffered space.
void logMessage(const char *format, ...) {
  if (!USBSerial) return;
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(logBuffer, sizeof(logBuffer), format, arguments);
  va_end(arguments);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(logBuffer)
      || USBSerial.availableForWrite() < length) {
    ++logDropped;
    return;
  }
  if (USBSerial.write(reinterpret_cast<const uint8_t *>(logBuffer), length)
      != static_cast<size_t>(length)) ++logDropped;
}

const char *moodName(PetMood mood) {
  static const char *names[] = {"neutral", "happy", "sad", "surprised", "sleepy", "annoyed", "dizzy"};
  const unsigned i = static_cast<unsigned>(mood);
  return i < 7 ? names[i] : "neutral";
}

const char *skinName(uint8_t skin) {
  switch (skin) {
    case 0: return "mono";
    case 1: return "sage";
    case 2: return "cat";
    default: return "unknown";
  }
}

bool skinReady(uint8_t skin) {
  switch (skin) {
    case 0: return true;
    case 1: return sage.ready();
    case 2: return blackCat.ready();
    default: return false;
  }
}

struct ActiveMetrics {
  const char *animation;
  uint32_t animationsStarted, collisions;
  float x, y, speed;
  int16_t top, bottom;
};

// Explicit declaration prevents Arduino's auto-prototype pass from placing
// this return type before the struct definition.
ActiveMetrics activeMetrics();

ActiveMetrics activeMetrics() {
  if (selectedSkin == 1)
    return {sage.animationName(),sage.animationsStarted(),sage.collisionCount(),
            sage.positionX(),sage.positionY(),sage.motionSpeed(),
            sage.drawTop(),sage.drawBottom()};
  if (selectedSkin == 2)
    return {blackCat.animationName(),blackCat.animationsStarted(),blackCat.collisionCount(),
            blackCat.positionX(),blackCat.positionY(),blackCat.motionSpeed(),
            blackCat.drawTop(),blackCat.drawBottom()};
  return {face.animationName(),face.animationsStarted(),face.collisionCount(),
          face.positionX(),face.positionY(),face.motionSpeed(),
          face.drawTop(),face.drawBottom()};
}

void renderActive(uint32_t now, bool isSleeping) {
  if (selectedSkin == 1) sage.render(now,isSleeping);
  else if (selectedSkin == 2) blackCat.render(now,isSleeping);
  else face.render(now,isSleeping);
}

void observeActive(float x, float y, float rotation, uint32_t now) {
  if (selectedSkin == 1) sage.observeMotion(x,y,rotation,now);
  else if (selectedSkin == 2) blackCat.observeMotion(x,y,rotation,now);
  else face.observeMotion(x,y,rotation,now);
}

bool readRegister(uint8_t address, uint8_t reg, uint8_t *out, size_t count) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, count) != count) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < count; ++i) out[i] = Wire.read();
  return true;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

void reportEvent(const char *event) {
  logMessage("{\"event\":\"%s\",\"ms\":%lu,\"mood\":\"%s\"}\n",
    event, static_cast<unsigned long>(millis()), moodName(face.mood()));
}

void changeMood(PetMood mood, uint32_t duration, uint32_t now) {
  face.setMood(mood, now);
  sage.setMood(mood, now);
  blackCat.setMood(mood, now);
  moodDeadline = duration ? now + duration : 0;
  nextIdle = now + random(35000, 65001);
}

void sleepPet(uint32_t now) {
  sleeping = true;
  monitorOpen = false;
  fullRefresh = true;
  blackCat.cancelTouch();
  catContact = false;
  changeMood(PetMood::Sleepy, 0, now);
  reportEvent("sleep");
}

void wakePet(uint32_t now) {
  sleeping = false;
  monitorOpen = false;
  fullRefresh = true;
  blackCat.cancelTouch();
  changeMood(PetMood::Happy, 3500, now);
  linearShake.reset();
  reportEvent("wake");
}

void tapPet(uint32_t now) {
  ++taps;
  tapStreak = now - lastTap < 1800 ? min(6, tapStreak + 1) : 1;
  lastTap = now;
  if (selectedSkin == 2) {
    blackCat.tapPulse(gesture.x(),gesture.y(),now);
    if (face.mood() == PetMood::Dizzy) {
      // Touch briefly steadies the eyes; the six-second dizzy spell persists.
      tapStreak = 0;
      reportEvent("touch_steady");
      return;
    }
    if (face.mood() == PetMood::Happy && tapStreak < 4) {
      // Keep the ongoing blep/wink clip instead of visibly resetting it on
      // every tap. The overlay above still acknowledges this tap immediately.
      moodDeadline = now + 4500;
      nextIdle = now + random(35000,65001);
      reportEvent("touch");
      return;
    }
  }
  changeMood(tapStreak >= 4 ? PetMood::Annoyed : PetMood::Happy,
             tapStreak >= 4 ? 2500 : 4500, now);
  reportEvent("touch");
}

void openPcMonitor(uint32_t now) {
  if (monitorOpen || cardOpen) return;
  blackCat.cancelTouch();
  catContact = false;
  menuOpen = false;
  monitorOpen = monitorDirty = true;
  monitorView = PcMonitorPage::View::Overview;
  monitorLastDraw = 0;
  sleeping = false;
  linearShake.reset();
  reportEvent("pc_monitor_open");
}

void returnFromPcMonitor(uint32_t now) {
  if (!monitorOpen) return;
  monitorOpen = false;
  menuOpen = menuDirty = true;
  linearShake.reset();
  reportEvent("pc_monitor_back");
}

void openPetMenu(uint32_t now) {
  if (menuOpen || cardOpen) return;
  monitorOpen = false;
  webPageOpen = false;
  blackCat.cancelTouch();
  catContact = false;
  menuOpen = menuDirty = true;
  ++menuOpens;
  linearShake.reset();
  sleeping = false;
  changeMood(PetMood::Neutral, 0, now);
  reportEvent("menu_open");
}

void closePetMenu(uint32_t now) {
  monitorOpen = false;
  webPageOpen = false;
  menuOpen = false;
  fullRefresh = true;
  linearShake.reset();
  nextIdle = now + random(35000,65001);
  reportEvent("menu_close");
}

void openWebPage(uint32_t now) {
  if (cardOpen) return;
  blackCat.cancelTouch();
  catContact = false;
  menuOpen = monitorOpen = false;
  webPageOpen = webDirty = true;
  EspMessagePage::reset(webUi);
  lastWebDraw = 0;
  linearShake.reset();
  reportEvent("web_note_settings");
}

void backFromWebPage(uint32_t now) {
  if (!webPageOpen) return;
  // Also cancels a setup request that has not opened the AP yet.
  EspMessageService::cancelProvision();
  EspMessagePage::reset(webUi);
  webPageOpen = false;
  menuOpen = menuDirty = true;
  reportEvent("web_note_back");
}

void openWifiPage(uint32_t now) {
  if (cardOpen) return;
  openWebPage(now);
  webUi.view=EspMessagePage::View::Wifi;
  webUi.directWifi=true;
  reportEvent("wlan_settings");
}

void openCard(uint32_t now) {
  if (cardOpen || !messageSnapshot.hasCard) return;
  cardReturn = webPageOpen ? CardReturn::Web :
               monitorOpen ? CardReturn::Monitor :
               menuOpen ? CardReturn::Menu : CardReturn::Pet;
  blackCat.cancelTouch();
  catContact = false;
  gesture.cancel(); // Do not let the contact that was down before delivery dismiss it.
  cardOpen = cardDirty = true;
  cardRevisionShown = 0;
  cardTokenId = messageSnapshot.tokenId;
  menuOpen = monitorOpen = webPageOpen = false;
  linearShake.reset();
  reportEvent("web_note_open");
}

void restoreAfterCard(uint32_t now) {
  cardOpen = false;
  if (cardReturn == CardReturn::Web) webPageOpen = webDirty = true;
  else if (cardReturn == CardReturn::Monitor) monitorOpen = monitorDirty = true;
  else if (cardReturn == CardReturn::Menu) menuOpen = menuDirty = true;
  else fullRefresh = true;
  nextIdle = now + random(35000,65001);
}

void dismissCard(uint32_t now) {
  if (!cardOpen) return;
  // A verified card can open between two display frames. Its first tap must
  // still dismiss the revision instead of reopening it on the next poll.
  // A card from the previous device token must never dismiss a new token's card.
  const uint32_t revision=cardRevisionShown ? cardRevisionShown :
                          (messageSnapshot.hasCard ? messageSnapshot.revision : 0);
  if (cardTokenId==messageSnapshot.tokenId) EspMessageService::dismiss(revision);
  restoreAfterCard(now);
  reportEvent("web_note_dismiss");
}

void selectPet(uint8_t skin, uint32_t now, bool persist = true) {
  if (!skinReady(skin)) {
    logMessage("{\"error\":\"skin_unavailable\"}\n");
    return;
  }
  if (skin != selectedSkin && persist) {
    selectionSaved = preferencesReady && preferences.putUChar("skin",skin) == 1;
  }
  selectedSkin = skin;
  monitorOpen = false;
  ++skinSelections;
  sleeping = false;
  if (skin == 0) face.begin(&canvas);
  else if (skin == 1) sage.begin(&canvas,now);
  else blackCat.begin(&canvas,now);
  changeMood(PetMood::Neutral,0,now);
  closePetMenu(now);
  reportEvent("skin_selected");
}

void handleTouchAction(TouchGesture::Action action, uint32_t now) {
  using A = TouchGesture::Action;
  if (cardOpen) {
    if (action == A::Tap) dismissCard(now);
    return; // No pet Tap, mood change, menu opening, or long-press side effect.
  }
  if (webPageOpen) {
    if (action == A::Tap) {
      const auto hit = EspMessagePage::tap(webUi,gesture.x(),gesture.y(),messageSnapshot);
      webDirty = true;
      if (hit == EspMessagePage::Action::Back) backFromWebPage(now);
      else if (hit == EspMessagePage::Action::Latest && messageSnapshot.hasCard)
        openCard(now);
    }
    return;
  }
  if (monitorOpen) {
    if (action == A::Tap) {
      const auto hit = PcMonitorPage::hit(gesture.x(),gesture.y(),monitorView);
      if (hit == PcMonitorPage::Action::Back) returnFromPcMonitor(now);
      else if (hit == PcMonitorPage::Action::Overview) {
        monitorView = PcMonitorPage::View::Overview; monitorDirty = true;
      } else if (hit == PcMonitorPage::Action::Cpu) {
        monitorView = PcMonitorPage::View::Cpu; monitorDirty = true;
      } else if (hit == PcMonitorPage::Action::Gpu) {
        monitorView = PcMonitorPage::View::Gpu; monitorDirty = true;
      }
    }
    return;
  }
  if (action == A::OpenMenu) openPetMenu(now);
  else if (action == A::HoldSleep && !menuOpen) { ++holds; sleepPet(now); }
  else if (action == A::Tap) {
    if (!menuOpen) tapPet(now);
    else {
      const int item = PetMenu::hit(gesture.x(),gesture.y());
      if (item == 3) openPcMonitor(now);
      else if (item == 4) openWebPage(now);
      else if (item == 5) openWifiPage(now);
      else if (item >= 0 && item <= 2) selectPet(static_cast<uint8_t>(item),now);
      else if (item == -1) closePetMenu(now);
    }
  }
}

void onTouchDown(uint16_t x, uint16_t y, uint32_t now) {
  pressed = true;
  menuButtonContact = catContact = false;
  if (cardOpen || webPageOpen) {
    wakeTouch = false;
    handleTouchAction(gesture.down(x,y,now,true),now);
    return;
  }
  wakeTouch = sleeping;
  if (wakeTouch) { wakePet(now); gesture.cancel(); return; }
  if (monitorOpen) {
    handleTouchAction(gesture.down(x,y,now,true),now);
    return;
  }
  if (!menuOpen && PetMenu::buttonHit(x,y)) {
    menuButtonContact = true;
    gesture.cancel();
    openPetMenu(now);
    return;
  }
  catContact = selectedSkin == 2 && !menuOpen;
  // If the roaming eye pair reaches the top edge, touching the eyes still
  // drags/pets them. The backup menu swipe remains available from empty top
  // edge space, and the visible corner button always opens the menu.
  const bool onCat = catContact &&
    CatTouchModel::faceZone(x,y,blackCat.positionX(),blackCat.positionY());
  if (catContact) blackCat.touchDown(x,y,now);
  handleTouchAction(gesture.down(x,y,now,menuOpen,catContact,!onCat),now);
}

void onTouchMove(uint16_t x, uint16_t y, uint32_t now) {
  if (menuButtonContact) return;
  const auto action = gesture.move(x,y,now);
  if (action == TouchGesture::Action::OpenMenu) {
    handleTouchAction(action,now);
    return;
  }
  if (catContact) {
    const auto started = blackCat.touchMove(x,y);
    if (started == CatTouchModel::Mode::Stroke) reportEvent("cat_stroke");
    else if (started == CatTouchModel::Mode::Drag) reportEvent("cat_drag");
  }
  handleTouchAction(action,now);
}

void onTouchUp(uint32_t now) {
  pressed = false;
  if (catContact) blackCat.touchUp(now);
  if (!wakeTouch && !menuButtonContact)
    handleTouchAction(gesture.release(now),now);
  else gesture.cancel();
  wakeTouch = menuButtonContact = catContact = false;
}

bool initializeTouch() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, HIGH); delay(20);
  digitalWrite(TOUCH_RST, LOW); delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(200);
  uint8_t chip = 0;
  if (!readRegister(TOUCH_ADDRESS, 0xA7, &chip, 1)) return false;
  // Same periodic-touch interrupt configuration as Waveshare's drawing example.
  if (!writeRegister(TOUCH_ADDRESS, 0xFA, 0x40)) return false;
  pinMode(TOUCH_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TOUCH_INT), touchInterrupt, FALLING);
  logMessage("{\"touch_chip_id\":%u}\n", chip);
  return true;
}

void pollTouch(uint32_t now) {
  if (!touchReady || now - lastTouchPoll < 12) return;
  lastTouchPoll = now;
  if (touchPending) {
    touchPending = false;
    uint8_t packet[5];
    if (readRegister(TOUCH_ADDRESS, 0x02, packet, sizeof(packet))) {
      const uint8_t fingers = packet[0] & 0x0F;
      const unsigned x = ((packet[1] & 0x0F) << 8) | packet[2];
      const unsigned y = ((packet[3] & 0x0F) << 8) | packet[4];
      if (fingers == 1 && x < 240 && y < 280) {
        touchLastSample = now;
        if (!pressed) onTouchDown(x,y,now);
        else if (!wakeTouch) onTouchMove(x,y,now);
      } else if (fingers == 0) {
        if (pressed) onTouchUp(now);
      }
    }
  }
  // A stale coordinate after release must never become an accidental long-press.
  if (pressed && now - touchLastSample > TOUCH_RELEASE_MS) onTouchUp(now);
  if (pressed && !wakeTouch) handleTouchAction(gesture.tick(now),now);
}

void triggerDizzy(uint32_t now, bool physical) {
  sleeping = false;
  monitorOpen = false;
  fullRefresh = true;
  changeMood(PetMood::Dizzy, DIZZY_MS, now);
  lastShake = now;
  linearShake.reset();
  if (physical) ++shakes;
  reportEvent(physical ? "shake" : "simulated_dizzy");
}

void pollImu(uint32_t now) {
  if (!imuReady || now - lastImuPoll < 20) return;
  lastImuPoll = now;
  if (!imu.getAccelerometer(ax, ay, az)) return;
  const bool gyroRead = imu.getGyroscope(gx, gy, gz);
  ++imuSamples;
  accelerationG = sqrtf(ax * ax + ay * ay + az * az);
  if (!isfinite(accelerationG) || accelerationG > 7.0f) return;
  if (!accelPrimed) {
    gravityX = ax; gravityY = ay; gravityZ = az;
    accelPrimed = true;
    return;
  }
  gravityX += 0.12f * (ax - gravityX);
  gravityY += 0.12f * (ay - gravityY);
  gravityZ += 0.12f * (az - gravityZ);
  const float dx = ax - gravityX, dy = ay - gravityY, dz = az - gravityZ;
  const float motion = sqrtf(dx * dx + dy * dy + dz * dz);
  const float angular = gyroRead ? sqrtf(gx * gx + gy * gy + gz * gz) : 0;
  // R5 observed: up->left, down->right, left->down, right->up.
  // Rotate the complete input 90 degrees: newX=-oldY, newY=oldX,
  // including wrist-rock terms. Right/down are positive; keep Z tilt unchanged.
  const float screenX = -dy + (gyroRead ? gx * .002f : 0);
  const float screenY = dx + (gyroRead ? gy * .002f : 0);
  if (!menuOpen && !monitorOpen && !webPageOpen && !cardOpen)
    observeActive(screenX,screenY,gyroRead ? -gz : 0,now);
  motionPeakG = fmaxf(motionPeakG, motion);
  if (isfinite(angular)) gyroPeakDps = fmaxf(gyroPeakDps, angular);
  if (sleeping || menuOpen || monitorOpen || webPageOpen || cardOpen || now < 3000 ||
      (lastShake && now - lastShake < SHAKE_COOLDOWN_MS)) {
    linearShake.reset();
    return;
  }
  // Deliberate back-and-forth linear shaking only. Gyroscope still drives the
  // face above, but wrist rotation alone must not trigger the dizzy state.
  if (linearShake.update(dx, dy, dz, now)) triggerDizzy(now, true);
}

void printStatus(uint32_t now) {
  const float fps = lastStats ? (frames - statsFrames) * 1000.0f / max(1UL, now - lastStats) : 0;
  const ActiveMetrics active = activeMetrics();
  logMessage("{\"version\":\"%s\",\"uptime_ms\":%lu,\"display\":%s,\"touch\":%s,\"imu\":%s,\"psram\":%u,\"heap\":%u,\"mood\":\"%s\",\"sleeping\":%s,\"fps\":%.1f,\"imu_samples\":%lu,\"acc_g\":%.3f,\"taps\":%lu,\"holds\":%lu,\"shakes\":%lu,\"frames\":%lu,\"loop_gap_max_ms\":%lu,\"log_dropped\":%lu,\"animation\":\"%s\",\"animations_started\":%lu,\"face_x\":%.1f,\"face_y\":%.1f,\"collisions\":%lu,\"motion_speed\":%.1f,\"eye_round\":%.2f,\"lid_left\":%.2f,\"lid_right\":%.2f}\n",
    VERSION, static_cast<unsigned long>(now), displayReady ? "true" : "false",
    touchReady ? "true" : "false", imuReady ? "true" : "false", ESP.getPsramSize(), ESP.getFreeHeap(),
    moodName(face.mood()), sleeping ? "true" : "false", fps, static_cast<unsigned long>(imuSamples),
    accelerationG, static_cast<unsigned long>(taps), static_cast<unsigned long>(holds), static_cast<unsigned long>(shakes),
    static_cast<unsigned long>(frames), static_cast<unsigned long>(loopGapMaxMs), static_cast<unsigned long>(logDropped),
    active.animation,static_cast<unsigned long>(active.animationsStarted),
    active.x,active.y,static_cast<unsigned long>(active.collisions),active.speed,
    face.eyeRoundness(), face.leftLidDrop(), face.rightLidDrop());
  lastStats = now;
  statsFrames = frames;
  logMessage("{\"motion_window\":true,\"ms\":%lu,\"peak_g\":%.3f,\"peak_dps\":%.1f,\"linear_reversals\":%u,\"shake_mode\":\"linear_handheld\"}\n",
    static_cast<unsigned long>(now), motionPeakG, gyroPeakDps,
    linearShake.reversals());
  motionPeakG = 0;
  gyroPeakDps = 0;
  logMessage("{\"ui\":true,\"ms\":%lu,\"menu_open\":%s,\"skin\":\"%s\",\"sage_ready\":%s,\"cat_ready\":%s,\"selection_saved\":%s,\"menu_opens\":%lu,\"skin_selections\":%lu}\n",
    static_cast<unsigned long>(now),menuOpen?"true":"false",skinName(selectedSkin),
    sage.ready()?"true":"false",blackCat.ready()?"true":"false",selectionSaved?"true":"false",
    static_cast<unsigned long>(menuOpens),static_cast<unsigned long>(skinSelections));
  logMessage("{\"cat_touch\":true,\"ms\":%lu,\"mode\":\"%s\",\"strokes\":%lu,\"drags\":%lu}\n",
    static_cast<unsigned long>(now), blackCat.touchModeName(),
    static_cast<unsigned long>(blackCat.strokes()),
    static_cast<unsigned long>(blackCat.drags()));
  logMessage("{\"pc_monitor\":true,\"ms\":%lu,\"open\":%s,\"view\":\"%s\",\"online\":%s,\"samples\":%lu,\"renders\":%lu,\"cpu_pct\":%d,\"gpu_pct\":%d}\n",
    static_cast<unsigned long>(now), monitorOpen ? "true" : "false",
    PcMonitorPage::viewName(monitorView),
    PcMonitorPage::isLive(monitorSnapshot,now) ? "true" : "false",
    static_cast<unsigned long>(monitorSamples),static_cast<unsigned long>(monitorFrames),
    monitorSnapshot.cpuPct,monitorSnapshot.gpuPct);
  logMessage("{\"web_note\":true,\"ms\":%lu,\"configured\":%s,\"wifi_saved\":%s,\"wifi\":%s,\"clock\":%s,\"server\":%s,\"portal\":%s,\"scanning\":%s,\"scan_count\":%u,\"pair\":\"%s\",\"web_error\":\"%s\",\"card_open\":%s,\"revision\":%lu,\"dismissed\":%lu,\"http_requests\":%lu,\"warm_responses\":%lu,\"heap_min\":%u,\"psram_free\":%u,\"psram_min\":%u}\n",
    static_cast<unsigned long>(now),messageSnapshot.configured?"true":"false",
    messageSnapshot.wifiSaved?"true":"false",
    messageSnapshot.wifi?"true":"false",messageSnapshot.clock?"true":"false",
    messageSnapshot.server?"true":"false",messageSnapshot.portal?"true":"false",
    messageSnapshot.scanning?"true":"false",static_cast<unsigned>(messageSnapshot.scanCount),
    EspMessageService::pairStatusName(messageSnapshot.pair),messageSnapshot.error,
    cardOpen?"true":"false",static_cast<unsigned long>(messageSnapshot.revision),
    static_cast<unsigned long>(messageSnapshot.dismissed),
    static_cast<unsigned long>(messageSnapshot.httpRequests),
    static_cast<unsigned long>(messageSnapshot.warmResponses),
    ESP.getMinFreeHeap(),ESP.getFreePsram(),ESP.getMinFreePsram());
  // Never include SSIDs, passwords, IPs or tokens in routine serial diagnostics.
  logMessage("{\"card_transfer\":true,\"http\":%d,\"content_length\":%d,\"read_bytes\":%lu,\"expected_crc\":%lu,\"actual_crc\":%lu,\"elapsed_ms\":%lu}\n",
    messageSnapshot.cardHttpCode,messageSnapshot.cardContentLength,
    static_cast<unsigned long>(messageSnapshot.cardReadBytes),
    static_cast<unsigned long>(messageSnapshot.cardExpectedCrc),
    static_cast<unsigned long>(messageSnapshot.cardActualCrc),
    static_cast<unsigned long>(messageSnapshot.cardTransferMs));
  logMessage("{\"wlan_manager\":true,\"saved_count\":%u,\"busy\":%s,\"generation\":%lu,\"page\":%u}\n",
    static_cast<unsigned>(messageSnapshot.savedCount),messageSnapshot.wifiBusy?"true":"false",
    static_cast<unsigned long>(messageSnapshot.wifiGeneration),static_cast<unsigned>(webUi.view));
}

void receivePcMonitor(const char *command, uint32_t now) {
  const size_t length = strlen(command);
  if (length > PcMonitorProtocol::MaxLineBytes) return;
  if (!strncmp(command,"pcmon ",6)) {
    PcMonitorProtocol::Sample sample = {};
    if (!PcMonitorProtocol::parseSample(command,length,sample)) return;
    monitorSnapshot.cpuPct = sample.cpu_pct;
    monitorSnapshot.cpuMhz = sample.cpu_mhz;
    monitorSnapshot.cpuTempC = sample.cpu_temp_c;
    monitorSnapshot.gpuPct = sample.gpu_pct;
    monitorSnapshot.gpuTempC = sample.gpu_temp_c;
    monitorSnapshot.vramUsedMiB = sample.vram_used_mib;
    monitorSnapshot.vramTotalMiB = sample.vram_total_mib;
    monitorSnapshot.gpuPowerW = sample.gpu_power_w;
    monitorSnapshot.gpuCoreMhz = sample.gpu_clock_mhz;
    monitorSnapshot.receivedAtMs = now;
    monitorSnapshot.hasSample = true;
    ++monitorSamples;
    if (monitorOpen) monitorDirty = true;
  } else if (!strncmp(command,"pcinfo ",7)) {
    PcMonitorProtocol::Info info = {};
    if (!PcMonitorProtocol::parseInfo(command,length,info)) return;
    if (info.kind == PcMonitorProtocol::InfoKind::Cpu)
      memcpy(monitorSnapshot.cpuName,info.name,sizeof(info.name));
    else if (info.kind == PcMonitorProtocol::InfoKind::Gpu)
      memcpy(monitorSnapshot.gpuName,info.name,sizeof(info.name));
    else if (info.kind == PcMonitorProtocol::InfoKind::Cores) {
      monitorSnapshot.coresPhysical = info.physical_cores;
      monitorSnapshot.coresLogical = info.logical_cores;
    }
    if (monitorOpen) monitorDirty = true;
  }
}

void handleCommand(const char *command, uint32_t now) {
  if (!strncmp(command,"pcmon ",6) || !strncmp(command,"pcinfo ",7))
    receivePcMonitor(command,now);
  else if (!strcmp(command, "status")) printStatus(now);
  else if (!strcmp(command,"menu")) openPetMenu(now);
  else if (!strcmp(command,"back")) {
    if (cardOpen) dismissCard(now);
    else if (webPageOpen) backFromWebPage(now);
    else if (monitorOpen) returnFromPcMonitor(now);
    else closePetMenu(now);
  }
  else if (!strcmp(command,"webpage")) openWebPage(now);
  else if (!strcmp(command,"wlanpage")) openWifiPage(now);
  else if (!strcmp(command,"wlanscan")) {
    const bool accepted=EspMessageService::requestScan();
    logMessage("{\"wlan_scan_requested\":%s}\n",accepted?"true":"false");
  }
  else if (!strcmp(command,"wlanreset")) {
    const bool accepted=EspMessageService::clearWifi();
    logMessage("{\"wlan_clear_requested\":%s}\n",accepted?"true":"false");
  }
  else if (!strcmp(command,"webcard")) openCard(now);
  else if (!strcmp(command,"websetup")) EspMessageService::requestProvision();
  else if (!strcmp(command,"pcpage")) openPcMonitor(now);
  else if (!strcmp(command,"pc overview")) {
    if (monitorOpen) { monitorView = PcMonitorPage::View::Overview; monitorDirty = true; }
  }
  else if (!strcmp(command,"pc cpu")) {
    if (monitorOpen) { monitorView = PcMonitorPage::View::Cpu; monitorDirty = true; }
  }
  else if (!strcmp(command,"pc gpu")) {
    if (monitorOpen) { monitorView = PcMonitorPage::View::Gpu; monitorDirty = true; }
  }
  else if (!strcmp(command,"skin mono")) selectPet(0,now);
  else if (!strcmp(command,"skin sage")) selectPet(1,now);
  else if (!strcmp(command,"skin cat")) selectPet(2,now);
  else if (!strcmp(command, "sleep")) sleepPet(now);
  else if (!strcmp(command, "wake")) wakePet(now);
  else if (!strcmp(command, "dizzy")) triggerDizzy(now, false);
  else if (!strncmp(command, "mood ", 5)) {
    for (unsigned i = 0; i < 7; ++i) {
      PetMood mood = static_cast<PetMood>(i);
      if (!strcmp(command + 5, moodName(mood))) {
        sleeping = false;
        changeMood(mood, mood == PetMood::Neutral ? 0 : 6000, now);
        reportEvent("serial_mood");
        return;
      }
    }
    logMessage("{\"error\":\"unknown_mood\"}\n");
  } else if (!strncmp(command, "anim ", 5)) {
    if (selectedSkin) { logMessage("{\"error\":\"anim_requires_mono_skin\"}\n"); return; }
    if (face.playAnimation(command + 5, now)) {
      sleeping = false;
      moodDeadline = face.mood() == PetMood::Neutral ? 0 : now + 7000;
      nextIdle = now + 45000;
      reportEvent("serial_animation");
    } else logMessage("{\"error\":\"unknown_animation\"}\n");
  } else logMessage("{\"error\":\"unknown_command\"}\n");
}

void pollSerial(uint32_t now) {
  // USB diagnostics are optional: unplugging the host does not stop animation.
  for (unsigned n = 0; n < 64 && USBSerial.available(); ++n) {
    const char c = USBSerial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      serialLine[serialLength] = 0;
      if (!serialOverflow && serialLength) handleCommand(serialLine, now);
      serialLength = 0; serialOverflow = false;
    } else if (serialLength + 1 < sizeof(serialLine)) serialLine[serialLength++] = c;
    else serialOverflow = true;
  }
}

void setup() {
  pinMode(SYS_EN, OUTPUT); digitalWrite(SYS_EN, HIGH);
  pinMode(BUZZER, OUTPUT); digitalWrite(BUZZER, LOW);
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, LOW);
  USBSerial.setTxBufferSize(1024);
  USBSerial.begin(115200);
  USBSerial.setTxTimeoutMs(0);
  randomSeed(esp_random());
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  Wire.setTimeOut(30);
  displayReady = canvas.begin(40000000);
  if (displayReady) {
    face.begin(&canvas);
    sage.begin(&canvas,millis());
    blackCat.begin(&canvas,millis());
    preferencesReady = preferences.begin("pet-ui",false);
    selectionSaved = preferencesReady;
    selectedSkin = preferencesReady ? preferences.getUChar("skin",0) : 0;
    if (!skinReady(selectedSkin)) selectedSkin = 0;
    renderActive(millis(),false);
    PetMenu::drawButton(canvas,selectedSkin);
    canvas.flush();
    digitalWrite(LCD_BL, HIGH);
  }
  touchReady = initializeTouch();
  imuReady = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (imuReady) {
    imuReady = imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_2)
      && imu.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_112_1Hz, SensorQMI8658::LPF_MODE_2)
      && imu.enableAccelerometer() && imu.enableGyroscope();
  }
  Wire.setClock(400000);
  const uint32_t now = millis();
  changeMood(PetMood::Neutral, 0, now);
  if (!EspMessageService::begin())
    logMessage("{\"error\":\"web_service_init_failed\"}\n");
  messageSnapshot=EspMessageService::snapshot();
  reportEvent("boot");
  printStatus(now);
}

void loop() {
  const uint32_t now = millis();
  if (previousLoopAt) loopGapMaxMs = max(loopGapMaxMs, now - previousLoopAt);
  previousLoopAt = now;
  pollTouch(now);
  pollImu(now);
  pollSerial(now);
  if (webPageOpen && EspMessagePage::tick(webUi,now)) webDirty=true;
  if (!lastMessageCheck || now-lastMessageCheck>=100) {
    lastMessageCheck=now;
    const bool wasServer=messageSnapshot.server, wasPortal=messageSnapshot.portal;
    const bool wasWifi=messageSnapshot.wifi, wasScanning=messageSnapshot.scanning;
    const bool wasConfigured=messageSnapshot.configured;
    const auto wasPair=messageSnapshot.pair;
    const uint32_t wasScanGeneration=messageSnapshot.scanGeneration;
    const uint32_t wasWifiGeneration=messageSnapshot.wifiGeneration;
    const bool wasWifiBusy=messageSnapshot.wifiBusy;
    char wasError[sizeof(messageSnapshot.error)];
    memcpy(wasError,messageSnapshot.error,sizeof(wasError));
    const uint32_t wasRevision=messageSnapshot.revision;
    messageSnapshot=EspMessageService::snapshot();
    if (cardOpen && cardTokenId!=messageSnapshot.tokenId) {
      restoreAfterCard(now);
      reportEvent("web_note_credentials_changed");
    }
    if (webPageOpen && (wasServer!=messageSnapshot.server ||
        wasPortal!=messageSnapshot.portal || wasWifi!=messageSnapshot.wifi ||
        wasScanning!=messageSnapshot.scanning ||
        wasConfigured!=messageSnapshot.configured || wasPair!=messageSnapshot.pair ||
        wasScanGeneration!=messageSnapshot.scanGeneration ||
        wasWifiGeneration!=messageSnapshot.wifiGeneration || wasWifiBusy!=messageSnapshot.wifiBusy ||
        memcmp(wasError,messageSnapshot.error,sizeof(wasError))!=0 ||
        wasRevision!=messageSnapshot.revision)) webDirty=true;
    if (wasScanGeneration!=messageSnapshot.scanGeneration) webUi.scanPage=0;
    if (cardOpen && (wasServer!=messageSnapshot.server ||
        wasRevision!=messageSnapshot.revision)) cardDirty=true;
    if (!cardOpen && !webPageOpen && messageSnapshot.hasCard &&
        messageSnapshot.revision>messageSnapshot.dismissed) openCard(now);
  }
  if (!menuOpen && !monitorOpen && !webPageOpen && !cardOpen && !sleeping && moodDeadline &&
      static_cast<int32_t>(now - moodDeadline) >= 0)
    changeMood(PetMood::Neutral, 0, now);
  if (!menuOpen && !monitorOpen && !webPageOpen && !cardOpen && !sleeping && !pressed && !moodDeadline &&
      static_cast<int32_t>(now - nextIdle) >= 0)
    changeMood(random(0, 3) == 0 ? PetMood::Happy : PetMood::Sleepy, random(4000, 7001), now);
  if (displayReady && now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    if (menuOpen) {
      if (menuDirty) {
        PetMenu::draw(canvas,sage,blackCat,selectedSkin);
        canvas.flush();
        menuDirty = false;
        ++frames;
      }
      return;
    }
    if (monitorOpen) {
      if (monitorDirty || !monitorLastDraw || now-monitorLastDraw >= 1000) {
        PcMonitorPage::draw(canvas,monitorSnapshot,monitorView,now);
        canvas.flush();
        monitorDirty = false;
        monitorLastDraw = now;
        ++monitorFrames;
        ++frames;
      }
      return;
    }
    if (webPageOpen) {
      if (webDirty) {
        EspMessagePage::draw(canvas,messageSnapshot,webUi);
        canvas.flush();
        webDirty=false;
        lastWebDraw=now;
        ++frames;
      }
      return;
    }
    if (cardOpen) {
      if (cardDirty || cardRevisionShown!=messageSnapshot.revision) {
        uint32_t drawnRevision=0;
        if (EspMessageService::drawCard(canvas,drawnRevision)) {
          if (!messageSnapshot.server) EspMessagePage::offlineBadge(canvas);
          canvas.flush();
          cardRevisionShown=drawnRevision;
          cardDirty=false;
          ++frames;
        }
      }
      return;
    }
    renderActive(now,sleeping);
    // Draw last so the menu control remains legible if the face drifts beneath
    // it. Its static pixels are sent at boot/after menu close (fullRefresh),
    // and in any eye dirty rectangle that overlaps it during cat motion.
    PetMenu::drawButton(canvas,selectedSkin);
    // The framebuffer is still cleared and redrawn in full. For the black cat,
    // send the eye pair and the small tongue as separate dirty rectangles so
    // a 30-pixel blep does not force an extra full-width LCD transfer band.
    const ActiveMetrics active = activeMetrics();
    const int16_t currentTop = active.top;
    const int16_t currentBottom = active.bottom;
    const int16_t top = fullRefresh ? 0 : min(previousDrawTop,currentTop);
    const int16_t bottom = fullRefresh ? 279 : max(previousDrawBottom,currentBottom);
    uint16_t* frameBuffer = canvas.getFramebuffer();
    if (fullRefresh) {
      lcd.draw16bitRGBBitmap(0, 0, frameBuffer, 240, 280);
    } else if (selectedSkin == 2) {
      using Rect = BlackCatPet::Rect;
      const auto valid = [](const Rect& r) { return r.left <= r.right && r.top <= r.bottom; };
      const auto united = [&](const Rect& a,const Rect& b) -> Rect {
        if (!valid(a)) return b;
        if (!valid(b)) return a;
        return {min(a.left,b.left),min(a.top,b.top),
                max(a.right,b.right),max(a.bottom,b.bottom)};
      };
      const Rect currentEyes = blackCat.eyeRect();
      const Rect currentTongue = blackCat.tongueRect();
      const Rect updates[2] = {united(previousCatEyes,currentEyes),
                               united(previousCatTongue,currentTongue)};
      lcd.startWrite();
      for (const Rect& r : updates) {
        if (!valid(r)) continue;
        const uint16_t width = r.right-r.left+1;
        lcd.writeAddrWindow(r.left,r.top,width,r.bottom-r.top+1);
        for (int y=r.top; y<=r.bottom; ++y)
          lcd.writePixels(frameBuffer+y*240+r.left,width);
      }
      lcd.endWrite();
    } else {
      lcd.draw16bitRGBBitmap(0, top, frameBuffer + top * 240, 240, bottom - top + 1);
    }
    if (selectedSkin == 2) {
      previousCatEyes = blackCat.eyeRect();
      previousCatTongue = blackCat.tongueRect();
    } else {
      previousCatEyes = {0,0,-1,-1};
      previousCatTongue = {0,0,-1,-1};
    }
    previousDrawTop = currentTop;
    previousDrawBottom = currentBottom;
    fullRefresh = false;
    ++frames;
  }
  if (USBSerial && now - lastStats >= 5000) printStatus(now);
  delay(1);
}
