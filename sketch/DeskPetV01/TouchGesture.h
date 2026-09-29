#pragma once
#include <stdint.h>

// Contact-level recognizer, independent of Arduino and the touch controller.
// The caller owns contact detection (including its 180ms stale-contact release),
// sleep/wake state and menu item hit-testing. Call tick() while contact is live.
class TouchGesture {
 public:
  enum class Action : uint8_t { None, Tap, HoldSleep, OpenMenu };
  static constexpr int16_t EdgeHeight = 36;
  static constexpr int16_t SwipeDistance = 64;
  static constexpr int16_t SwipeHorizontalLimit = 40;
  static constexpr int16_t TapSlop = 12;
  static constexpr uint32_t SwipeDeadlineMs = 1000;
  static constexpr uint32_t MenuTapDeadlineMs = 1000;
  static constexpr uint32_t HoldSleepMs = 2000;

  // Most skins keep the existing immediate Tap. The black-cat skin requests a
  // deferred semantic Tap, so a stroke/drag can respond visually on DOWN but
  // cannot increment the tap streak unless the contact ends as a real tap.
  // Top-edge and menu contacts also defer Tap; a menu drag never selects.
  // Do not call cancel()/down() just because OpenMenu changed the visible view:
  // this contact stays consumed until release(), then a new down() is required.
  Action down(uint16_t x, uint16_t y, uint32_t now, bool menuOpen = false,
              bool deferMainTap = false, bool allowEdgeSwipe = true) {
    if (active_) return Action::None;
    active_ = true;
    consumed_ = moved_ = false;
    menuContact_ = menuOpen;
    startX_ = x_ = x;
    startY_ = y_ = y;
    downAt_ = now;
    maxHorizontal_ = 0;
    edgeCandidate_ = !menuOpen && allowEdgeSwipe && y <= EdgeHeight;
    deferredTap_ = menuOpen || edgeCandidate_ || deferMainTap;
    return deferredTap_ ? Action::None : Action::Tap;
  }

  Action move(uint16_t x, uint16_t y, uint32_t now) {
    if (!active_) return Action::None;
    x_ = x;
    y_ = y;
    const int32_t dx = static_cast<int32_t>(x) - startX_;
    const int32_t dy = static_cast<int32_t>(y) - startY_;
    const uint32_t horizontal = dx < 0 ? static_cast<uint32_t>(-dx) : static_cast<uint32_t>(dx);
    if (horizontal > maxHorizontal_) maxHorizontal_ = horizontal;
    // Short-circuit the squared calculation outside the small slop range, so
    // even arbitrary uint16_t input coordinates cannot overflow the products.
    if (dx < -TapSlop || dx > TapSlop || dy < -TapSlop || dy > TapSlop ||
        dx * dx + dy * dy > TapSlop * TapSlop) moved_ = true;
    if (consumed_) return Action::None;
    if (edgeCandidate_ && (maxHorizontal_ > SwipeHorizontalLimit ||
                           now - downAt_ > SwipeDeadlineMs)) edgeCandidate_ = false;
    if (edgeCandidate_ && dy >= SwipeDistance) {
      consumed_ = true;
      return Action::OpenMenu;
    }
    return tick(now);
  }

  Action tick(uint32_t now) {
    if (!active_ || consumed_) return Action::None;
    if (edgeCandidate_ && now - downAt_ > SwipeDeadlineMs) edgeCandidate_ = false;
    if (!menuContact_ && !moved_ && now - downAt_ >= HoldSleepMs) {
      consumed_ = true;
      return Action::HoldSleep;
    }
    return Action::None;
  }

  // x()/y() remain the final contact coordinates after release for hit-testing.
  Action release(uint32_t now) {
    if (!active_) return Action::None;
    active_ = false;
    if (consumed_ || moved_ || !deferredTap_) return Action::None;
    const uint32_t elapsed = now - downAt_;
    if (menuContact_ ? elapsed > MenuTapDeadlineMs : elapsed >= HoldSleepMs)
      return Action::None;
    return Action::Tap;
  }

  void cancel() { active_ = false; consumed_ = true; }
  bool active() const { return active_; }
  uint16_t x() const { return x_; }
  uint16_t y() const { return y_; }

 private:
  uint16_t startX_ = 0, startY_ = 0, x_ = 0, y_ = 0;
  uint32_t downAt_ = 0, maxHorizontal_ = 0;
  bool active_ = false, consumed_ = false, moved_ = false;
  bool menuContact_ = false, edgeCandidate_ = false, deferredTap_ = false;
};
