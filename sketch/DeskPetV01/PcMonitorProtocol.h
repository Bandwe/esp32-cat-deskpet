#pragma once

// Bounded, allocation-free parser for the PC telemetry sent over USB serial.
// Pass the bytes of one complete line WITHOUT CR/LF. A rejected line never
// changes the caller's output object. This header deliberately uses no Arduino
// types so the same parser can be exercised by host-side tests.

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

namespace PcMonitorProtocol {

static const size_t MaxLineBytes = 128;
static const size_t MaxNameBytes = 40;

struct Sample {
  int cpu_pct;
  int cpu_mhz;
  int cpu_temp_c;
  int gpu_pct;
  int gpu_temp_c;
  int vram_used_mib;
  int vram_total_mib;
  int gpu_power_w;
  int gpu_clock_mhz;
};

enum class InfoKind : uint8_t { None = 0, Cpu, Gpu, Cores };

struct Info {
  InfoKind kind;
  char name[MaxNameBytes + 1];
  int physical_cores;
  int logical_cores;
};

namespace Detail {

inline bool printableAscii(const char* data, size_t length) {
  if (data == nullptr || length == 0 || length > MaxLineBytes) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

inline bool literal(const char* data, size_t length, size_t& at,
                    const char* word) {
  size_t index = at;
  for (size_t i = 0; word[i] != '\0'; ++i) {
    if (index >= length || data[index] != word[i]) return false;
    ++index;
  }
  at = index;
  return true;
}

// At least one ASCII space is required between tokens. No trailing space.
inline bool spaces(const char* data, size_t length, size_t& at) {
  if (at >= length || data[at] != ' ') return false;
  do {
    ++at;
  } while (at < length && data[at] == ' ');
  return at < length;
}

// Only -1 is a valid negative token. Overflow is rejected before multiplying.
inline bool integer(const char* data, size_t length, size_t& at, int& value) {
  if (at >= length) return false;
  if (data[at] == '-') {
    if (at + 1 >= length || data[at + 1] != '1') return false;
    at += 2;
    if (at < length && data[at] != ' ') return false;
    value = -1;
    return true;
  }
  if (data[at] < '0' || data[at] > '9') return false;
  int number = 0;
  do {
    const int digit = data[at] - '0';
    if (number > (INT_MAX - digit) / 10) return false;
    number = number * 10 + digit;
    ++at;
  } while (at < length && data[at] >= '0' && data[at] <= '9');
  if (at < length && data[at] != ' ') return false;
  value = number;
  return true;
}

}  // namespace Detail

// Wire format:
// pcmon cpu_pct cpu_mhz cpu_temp_c gpu_pct gpu_temp_c
//       vram_used_mib vram_total_mib gpu_power_w gpu_clock_mhz
// Each integer may be -1 for unavailable. Other negatives are rejected.
inline bool parseSample(const char* data, size_t length, Sample& output) {
  if (!Detail::printableAscii(data, length)) return false;
  size_t at = 0;
  if (!Detail::literal(data, length, at, "pcmon") ||
      !Detail::spaces(data, length, at)) return false;

  const int maxima[9] = {
      100, 20000, 150, 100, 150, 1048576, 1048576, 2500, 20000};
  int values[9];
  for (size_t i = 0; i < 9; ++i) {
    if (!Detail::integer(data, length, at, values[i])) return false;
    if (values[i] > maxima[i]) return false;
    if (i < 8 && !Detail::spaces(data, length, at)) return false;
  }
  if (at != length) return false;  // An extra token or trailing spaces.
  if (values[5] >= 0 && values[6] >= 0 && values[5] > values[6]) return false;

  const Sample candidate = {
      values[0], values[1], values[2], values[3], values[4],
      values[5], values[6], values[7], values[8]};
  output = candidate;
  return true;
}

// Optional identity lines:
//   pcinfo cpu <printable ASCII name, 1..40 bytes>
//   pcinfo gpu <printable ASCII name, 1..40 bytes>
//   pcinfo cores <physical: 1..512> <logical: physical..1024>
inline bool parseInfo(const char* data, size_t length, Info& output) {
  if (!Detail::printableAscii(data, length)) return false;
  size_t at = 0;
  if (!Detail::literal(data, length, at, "pcinfo") ||
      !Detail::spaces(data, length, at)) return false;

  Info candidate = {};
  const size_t kindAt = at;
  const bool isCpu = Detail::literal(data, length, at, "cpu");
  const bool isGpu = !isCpu && Detail::literal(data, length, at, "gpu");
  if (isCpu || isGpu) {
    candidate.kind = isCpu ? InfoKind::Cpu : InfoKind::Gpu;
    if (at >= length || data[at] != ' ') return false;
    ++at;
    const size_t nameLength = length - at;
    if (nameLength == 0 || nameLength > MaxNameBytes ||
        data[at] == ' ' || data[length - 1] == ' ') return false;
    for (size_t i = 0; i < nameLength; ++i) candidate.name[i] = data[at + i];
    candidate.name[nameLength] = '\0';
  } else {
    at = kindAt;
    if (!Detail::literal(data, length, at, "cores") ||
        !Detail::spaces(data, length, at)) return false;
    candidate.kind = InfoKind::Cores;
    if (!Detail::integer(data, length, at, candidate.physical_cores) ||
        !Detail::spaces(data, length, at) ||
        !Detail::integer(data, length, at, candidate.logical_cores) ||
        at != length) return false;
    if (candidate.physical_cores < 1 || candidate.physical_cores > 512 ||
        candidate.logical_cores < candidate.physical_cores ||
        candidate.logical_cores > 1024) return false;
  }
  output = candidate;
  return true;
}

}  // namespace PcMonitorProtocol
