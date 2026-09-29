#pragma once

#include <Arduino_GFX_Library.h>
#include <stdint.h>
#include <stdio.h>

// The PC companion is the sole source of these values. -1 means that its
// collector could not provide the metric; the board does not estimate it.
namespace PcMonitorPage {

static constexpr uint16_t BLACK = 0x0000;
static constexpr uint16_t PANEL = 0x0841;
static constexpr uint16_t PANEL_EDGE = 0x2945;
static constexpr uint16_t TRACK = 0x3186;
static constexpr uint16_t MUTED = 0x8410;
static constexpr uint16_t WHITE = 0xffff;
static constexpr uint16_t GOLD = 0xfecf;
static constexpr uint32_t OFFLINE_AFTER_MS = 3000;

struct Snapshot {
  char cpuName[41] = {};
  char gpuName[41] = {};
  int coresPhysical = -1;
  int coresLogical = -1;
  int cpuPct = -1;
  int cpuMhz = -1;
  int cpuTempC = -1;
  int gpuPct = -1;
  int gpuTempC = -1;
  int vramUsedMiB = -1;
  int vramTotalMiB = -1;
  int gpuPowerW = -1;
  int gpuCoreMhz = -1;
  uint32_t receivedAtMs = 0;
  bool hasSample = false;
};

enum class View : uint8_t { Overview, Cpu, Gpu };
enum class Action : uint8_t { None, Back, Overview, Cpu, Gpu };

inline const char* viewName(View view) {
  switch (view) {
    case View::Overview: return "overview";
    case View::Cpu: return "cpu";
    case View::Gpu: return "gpu";
  }
  return "overview";
}

inline bool isLive(const Snapshot& snapshot, uint32_t now) {
  return snapshot.hasSample
      && static_cast<uint32_t>(now - snapshot.receivedAtMs) <= OFFLINE_AFTER_MS;
}

// The 34 x 34 back control and 70 x 34 tabs exceed the tiny drawn symbols.
inline Action hit(int x, int y, View current) {
  if (x < 0 || x >= 240 || y < 0 || y >= 280) return Action::None;
  if (x <= 49 && y <= 49) return Action::Back;
  if (y >= 239) {
    if (x <= 80) return Action::Overview;
    if (x >= 81 && x <= 159) return Action::Cpu;
    return Action::Gpu;
  }
  if (current == View::Overview) {
    if (x >= 10 && x <= 229 && y >= 58 && y <= 143) return Action::Cpu;
    if (x >= 10 && x <= 229 && y >= 151 && y <= 236) return Action::Gpu;
  }
  return Action::None;
}

inline Action hit(int x, int y) { return hit(x, y, View::Overview); }

inline void text(Arduino_Canvas& canvas, int x, int y, const char* value,
                 uint16_t color = WHITE, uint8_t size = 1) {
  canvas.setTextColor(color);
  canvas.setTextSize(size);
  canvas.setCursor(x, y);
  canvas.print(value);
}

inline void clippedName(Arduino_Canvas& canvas, int x, int y,
                        const char* value, size_t capacity, int maxChars) {
  char shortName[37] = {};
  int index = 0;
  while (index < maxChars && index < static_cast<int>(sizeof(shortName) - 1)
         && index < static_cast<int>(capacity) && value[index]) {
    const unsigned char c = static_cast<unsigned char>(value[index]);
    shortName[index] = (c >= 32 && c <= 126) ? static_cast<char>(c) : '?';
    ++index;
  }
  shortName[index] = 0;
  text(canvas, x, y, index ? shortName : "--", MUTED);
}

inline void integer(char* output, size_t length, int value, bool live,
                    const char* suffix = "") {
  if (!live || value < 0) snprintf(output, length, "--");
  else snprintf(output, length, "%ld%s", static_cast<long>(value), suffix);
}

inline void percent(char* output, size_t length, int value, bool live) {
  if (!live || value < 0 || value > 100) snprintf(output, length, "--");
  else snprintf(output, length, "%ld%%", static_cast<long>(value));
}

inline void compactMhz(char* output, size_t length, int mhz, bool live) {
  if (!live || mhz < 0) snprintf(output, length, "--");
  else {
    const int tenths = (mhz + 50) / 100;
    snprintf(output, length, "%ld.%ld GHz",
             static_cast<long>(tenths / 10),
             static_cast<long>(tenths % 10));
  }
}

inline void vram(char* output, size_t length, int used, int total,
                 bool live) {
  if (!live || used < 0) snprintf(output, length, "--");
  else if (total < 0) snprintf(output, length, "%ld", static_cast<long>(used));
  else snprintf(output, length, "%ld/%ld",
                static_cast<long>(used), static_cast<long>(total));
}

inline void loadBar(Arduino_Canvas& canvas, int x, int y, int width,
                    int pct, bool live) {
  canvas.fillRoundRect(x, y, width, 7, 3, TRACK);
  if (live && pct > 0 && pct <= 100) {
    const int filled = width * pct / 100;
    if (filled >= 3) canvas.fillRoundRect(x, y, filled, 7, 3, GOLD);
  }
}

inline void header(Arduino_Canvas& canvas, bool live) {
  canvas.drawRoundRect(8, 7, 34, 33, 9, PANEL_EDGE);
  canvas.drawLine(26, 16, 18, 23, WHITE);
  canvas.drawLine(18, 23, 26, 31, WHITE);
  canvas.drawLine(18, 23, 33, 23, WHITE);
  text(canvas, 51, 16, live ? "PC MONITOR" : "PC OFFLINE", WHITE, 2);
  if (!live) canvas.fillRoundRect(181, 12, 51, 25, 8, PANEL_EDGE);
  canvas.drawRoundRect(181, 12, 51, 25, 8, live ? GOLD : MUTED);
  text(canvas, live ? 193 : 184, 21, live ? "LIVE" : "NO DATA",
       live ? GOLD : WHITE);
  canvas.drawFastHLine(10, 48, 220, PANEL_EDGE);
}

inline void tabs(Arduino_Canvas& canvas, View active) {
  static constexpr int positions[3] = {8, 85, 162};
  static const char* const names[3] = {"ALL", "CPU", "GPU"};
  canvas.drawFastHLine(10, 240, 220, PANEL_EDGE);
  for (int i = 0; i < 3; ++i) {
    const bool selected = static_cast<uint8_t>(active) == static_cast<uint8_t>(i);
    canvas.fillRoundRect(positions[i], 247, 70, 28, 8, selected ? PANEL_EDGE : BLACK);
    canvas.drawRoundRect(positions[i], 247, 70, 28, 8, selected ? GOLD : PANEL_EDGE);
    text(canvas, positions[i] + 26, 256, names[i], selected ? GOLD : MUTED);
  }
}

inline void overviewCard(Arduino_Canvas& canvas, int top, const char* kind,
                         const char* name, size_t nameCapacity, int usage,
                         int temperature, int secondary, bool cpu,
                         bool live) {
  canvas.fillRoundRect(10, top, 220, 86, 11, PANEL);
  canvas.drawRoundRect(10, top, 220, 86, 11, PANEL_EDGE);
  canvas.fillRoundRect(16, top + 12, 3, 13, 1, GOLD);
  text(canvas, 25, top + 11, kind, GOLD);
  clippedName(canvas, 60, top + 11, name, nameCapacity, 27);

  char number[28] = {};
  percent(number, sizeof(number), usage, live);
  text(canvas, 22, top + 30, number, live ? WHITE : MUTED, 3);
  text(canvas, 22, top + 59, "LOAD", MUTED);

  text(canvas, 151, top + 30, "TEMP", MUTED);
  integer(number, sizeof(number), temperature, live, "C");
  text(canvas, 151, top + 42, number, live ? GOLD : MUTED, 2);
  text(canvas, cpu ? 155 : 150, top + 64, cpu ? "CLK" : "VRAM", MUTED);
  if (cpu) compactMhz(number, sizeof(number), secondary, live);
  else integer(number, sizeof(number), secondary, live, "M");
  text(canvas, cpu ? 177 : 180, top + 64, number, MUTED);
  loadBar(canvas, 22, top + 76, 196, usage, live);
}

inline void overview(Arduino_Canvas& canvas, const Snapshot& snapshot, bool live) {
  overviewCard(canvas, 57, "CPU", snapshot.cpuName, sizeof(snapshot.cpuName),
               snapshot.cpuPct, snapshot.cpuTempC, snapshot.cpuMhz, true, live);
  overviewCard(canvas, 150, "GPU", snapshot.gpuName, sizeof(snapshot.gpuName),
               snapshot.gpuPct, snapshot.gpuTempC, snapshot.vramUsedMiB, false, live);
}

inline void metricTile(Arduino_Canvas& canvas, int x, int y,
                       const char* label, const char* value, bool live) {
  canvas.fillRoundRect(x, y, 105, 36, 8, PANEL);
  canvas.drawRoundRect(x, y, 105, 36, 8, PANEL_EDGE);
  text(canvas, x + 8, y + 5, label, MUTED);
  int length = 0;
  while (value[length] && length < 24) ++length;
  text(canvas, x + 8, y + 18, value, live ? WHITE : MUTED,
       length <= 7 ? 2 : 1);
}

inline void detail(Arduino_Canvas& canvas, const Snapshot& snapshot,
                   View view, bool live) {
  const bool cpu = view == View::Cpu;
  canvas.fillRoundRect(10, 57, 220, 42, 10, PANEL);
  canvas.drawRoundRect(10, 57, 220, 42, 10, PANEL_EDGE);
  text(canvas, 20, 64, cpu ? "CPU / PROCESSOR" : "GPU / GRAPHICS", GOLD);
  clippedName(canvas, 20, 79, cpu ? snapshot.cpuName : snapshot.gpuName,
              cpu ? sizeof(snapshot.cpuName) : sizeof(snapshot.gpuName), 32);

  char number[32] = {};
  text(canvas, 18, 108, "UTILIZATION", MUTED);
  const int usage = cpu ? snapshot.cpuPct : snapshot.gpuPct;
  percent(number, sizeof(number), usage, live);
  text(canvas, 16, 120, number, live ? GOLD : MUTED, 3);
  loadBar(canvas, 16, 151, 208, usage, live);

  if (cpu) {
    integer(number, sizeof(number), snapshot.cpuTempC, live, "C");
    metricTile(canvas, 10, 163, "TEMP", number, live);
    integer(number, sizeof(number), snapshot.cpuMhz, live);
    metricTile(canvas, 125, 163, "CLOCK MHz", number, live);
    integer(number, sizeof(number), snapshot.coresPhysical, live);
    metricTile(canvas, 10, 202, "CORES", number, live);
    integer(number, sizeof(number), snapshot.coresLogical, live);
    metricTile(canvas, 125, 202, "THREADS", number, live);
  } else {
    integer(number, sizeof(number), snapshot.gpuTempC, live, "C");
    metricTile(canvas, 10, 163, "TEMP", number, live);
    integer(number, sizeof(number), snapshot.gpuCoreMhz, live);
    metricTile(canvas, 125, 163, "CORE MHz", number, live);
    vram(number, sizeof(number), snapshot.vramUsedMiB,
         snapshot.vramTotalMiB, live);
    metricTile(canvas, 10, 202, "VRAM MiB", number, live);
    integer(number, sizeof(number), snapshot.gpuPowerW, live, "W");
    metricTile(canvas, 125, 202, "POWER", number, live);
  }
}

inline void draw(Arduino_Canvas& canvas, const Snapshot& snapshot,
                 View view, uint32_t now) {
  const bool live = isLive(snapshot, now);
  canvas.fillScreen(BLACK);
  canvas.setTextWrap(false);
  header(canvas, live);
  if (view == View::Overview) overview(canvas, snapshot, live);
  else detail(canvas, snapshot, view, live);
  tabs(canvas, view);
}

} // namespace PcMonitorPage
