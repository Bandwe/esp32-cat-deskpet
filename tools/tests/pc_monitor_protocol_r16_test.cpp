// Pure host-side tests; no Arduino, USB serial connection, or flash access.
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../../sketch/DeskPetV01/PcMonitorProtocol.h"

namespace P = PcMonitorProtocol;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

bool sample(const std::string& line, P::Sample& output) {
  return P::parseSample(line.data(), line.size(), output);
}

bool info(const std::string& line, P::Info& output) {
  return P::parseInfo(line.data(), line.size(), output);
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

  suite.run("nine measurements preserve wire order", [] {
    P::Sample out = {};
    require(sample("pcmon 37 4900 62 88 75 20100 24000 216 2800", out),
            "ordinary sample was rejected");
    require(out.cpu_pct == 37 && out.cpu_mhz == 4900 && out.cpu_temp_c == 62 &&
            out.gpu_pct == 88 && out.gpu_temp_c == 75 &&
            out.vram_used_mib == 20100 && out.vram_total_mib == 24000 &&
            out.gpu_power_w == 216 && out.gpu_clock_mhz == 2800,
            "wire fields were reordered");
  });

  suite.run("-1 is accepted for every unavailable measurement", [] {
    P::Sample out = {};
    require(sample("pcmon -1 -1 -1 -1 -1 -1 -1 -1 -1", out),
            "unavailable sample was rejected");
    require(out.cpu_pct == -1 && out.gpu_temp_c == -1 &&
            out.gpu_clock_mhz == -1, "unavailable value changed");
  });

  suite.run("zero and upper bounds are accepted", [] {
    P::Sample out = {};
    require(sample("pcmon 0 0 0 0 0 0 0 0 0", out), "zero was rejected");
    require(sample("pcmon 100 20000 150 100 150 1048576 1048576 2500 20000", out),
            "specified upper bounds were rejected");
  });

  suite.run("multiple ASCII separators are accepted", [] {
    P::Sample out = {};
    require(sample("pcmon  37  4900 62   88 75 20100 24000 216 2800", out),
            "multiple spaces between tokens were rejected");
  });

  suite.run("exactly nine integers are required", [] {
    P::Sample out = {};
    require(!sample("pcmon 1 2 3 4 5 6 7 8", out), "eight values accepted");
    require(!sample("pcmon 1 2 3 4 5 6 7 8 9 10", out), "ten values accepted");
    require(!sample("pcmon 1 2 3 4 5 6 7 8 9 ", out), "trailing space accepted");
  });

  suite.run("prefix, leading text and token boundaries are exact", [] {
    P::Sample out = {};
    require(!sample("mon 1 2 3 4 5 6 7 8 9", out), "wrong prefix accepted");
    require(!sample(" pcmon 1 2 3 4 5 6 7 8 9", out), "leading space accepted");
    require(!sample("pcmonitor 1 2 3 4 5 6 7 8 9", out), "partial prefix accepted");
    require(!sample("pcmon 1x 2 3 4 5 6 7 8 9", out), "invalid token suffix accepted");
  });

  suite.run("only literal -1 is permitted below zero", [] {
    P::Sample out = {};
    require(!sample("pcmon -2 2 3 4 5 6 7 8 9", out), "-2 accepted");
    require(!sample("pcmon -01 2 3 4 5 6 7 8 9", out), "-01 accepted");
    require(!sample("pcmon +1 2 3 4 5 6 7 8 9", out), "+1 accepted");
  });

  suite.run("32-bit overflow and unreasonable ranges are rejected", [] {
    P::Sample out = {};
    require(!sample("pcmon 2147483648 2 3 4 5 6 7 8 9", out), "overflow accepted");
    require(!sample("pcmon 999999999999999999999999 2 3 4 5 6 7 8 9", out),
            "long overflow accepted");
    require(!sample("pcmon 101 2 3 4 5 6 7 8 9", out), "CPU above 100% accepted");
    require(!sample("pcmon 1 2 3 4 151 6 7 8 9", out), "hot GPU accepted");
    require(!sample("pcmon 1 2 3 4 5 8 7 8 9", out), "used VRAM above total accepted");
  });

  suite.run("control bytes, DEL, high bytes and oversized lines are rejected", [] {
    P::Sample out = {};
    const unsigned char highByte = 0x80;
    const std::string ordinary = "pcmon 1 2 3 4 5 6 7 8 9";
    require(!sample(ordinary + "\r", out), "CR accepted");
    require(!sample(ordinary + "\n", out), "LF accepted");
    require(!sample("pcmon\t1 2 3 4 5 6 7 8 9", out), "TAB accepted");
    require(!sample(ordinary + std::string(1, '\0') + "extra", out),
            "embedded NUL accepted");
    require(!sample(ordinary + std::string(1, char(0x7f)), out), "DEL accepted");
    require(!sample(ordinary + std::string(1, static_cast<char>(highByte)), out),
            "non-ASCII byte accepted");
    require(!sample(ordinary + std::string(128, ' '), out), "oversized line accepted");
  });

  suite.run("failed samples never overwrite the last good sample", [] {
    P::Sample out = {};
    require(sample("pcmon 37 4900 62 88 75 20100 24000 216 2800", out),
            "setup sample rejected");
    require(!sample("pcmon 200 4900 62 88 75 20100 24000 216 2800", out),
            "bad sample accepted");
    require(out.cpu_pct == 37 && out.gpu_clock_mhz == 2800,
            "rejected sample changed output");
  });

  suite.run("CPU and GPU identity names are copied and terminated", [] {
    P::Info out = {};
    require(info("pcinfo cpu AMD Ryzen 9 9950X", out), "CPU name rejected");
    require(out.kind == P::InfoKind::Cpu &&
            std::strcmp(out.name, "AMD Ryzen 9 9950X") == 0,
            "CPU identity incorrect");
    require(info("pcinfo gpu NVIDIA GeForce RTX 5070 Ti", out), "GPU name rejected");
    require(out.kind == P::InfoKind::Gpu &&
            std::strcmp(out.name, "NVIDIA GeForce RTX 5070 Ti") == 0,
            "GPU identity incorrect");
  });

  suite.run("identity has a 40-byte limit and no edge spaces", [] {
    P::Info out = {};
    const std::string forty(40, 'X');
    require(info("pcinfo gpu " + forty, out), "40-byte name rejected");
    require(out.name[40] == '\0' && std::strlen(out.name) == 40,
            "40-byte name was not terminated");
    require(!info("pcinfo gpu " + forty + "X", out), "41-byte name accepted");
    require(!info("pcinfo gpu ", out), "empty name accepted");
    require(!info("pcinfo gpu  Name", out), "leading name space accepted");
    require(!info("pcinfo gpu Name ", out), "trailing name space accepted");
  });

  suite.run("core counts are bounded and ordered", [] {
    P::Info out = {};
    require(info("pcinfo cores 16 32", out), "valid core counts rejected");
    require(out.kind == P::InfoKind::Cores && out.physical_cores == 16 &&
            out.logical_cores == 32, "core counts incorrect");
    require(!info("pcinfo cores 0 32", out), "zero physical cores accepted");
    require(!info("pcinfo cores 16 8", out), "logical < physical accepted");
    require(!info("pcinfo cores 513 1024", out), "physical above maximum accepted");
    require(!info("pcinfo cores 16 1025", out), "logical above maximum accepted");
    require(!info("pcinfo cores 16 32 64", out), "extra core field accepted");
  });

  suite.run("malformed identity lines leave output unchanged", [] {
    P::Info out = {};
    const unsigned char highByte = 0x80;
    require(info("pcinfo cpu Valid CPU", out), "setup identity rejected");
    require(!info("pcinfo cpu Bad\tName", out), "control character accepted");
    require(!info("pcinfo gpu " + std::string(1, static_cast<char>(highByte)), out),
            "non-ASCII identity accepted");
    require(!info("pcinfo disk SSD", out), "unknown identity type accepted");
    require(out.kind == P::InfoKind::Cpu && std::strcmp(out.name, "Valid CPU") == 0,
            "rejected identity changed output");
  });

  std::cout << "PC monitor protocol: " << suite.passed << " passed, "
            << suite.failed << " failed\n";
  return suite.failed == 0 ? 0 : 1;
}
