// Host-side byte equality test for every adopted source frame and palette.
// No renderer, serial port, or physical aesthetic validation is involved.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../sketch/DeskPetV01/SageAnimation.h"

static void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  try {
    require(argc == 2, "Usage: sage_assets_r8_test.exe path/to/sage_v03/device");
    std::vector<uint8_t> frame(SageAssets::kFrameBytes + 16, 0x5a);
    unsigned frameCount = 0;
    for (uint8_t i = 0; i < static_cast<uint8_t>(SageAssets::ClipId::Count); ++i) {
      const auto id = static_cast<SageAssets::ClipId>(i);
      const auto& asset = SageAssets::clip(id);
      std::string path = std::string(argv[1]) + "/" + asset.name + ".idx8.bin";
      std::ifstream stream(path, std::ios::binary);
      require(bool(stream), "Cannot open " + path);
      std::vector<uint8_t> raw((std::istreambuf_iterator<char>(stream)), {});
      require(raw.size() == 512 + size_t(asset.frameCount) * SageAssets::kFrameBytes,
              "Source byte length differs: " + path);
      for (unsigned entry = 0; entry < 256; ++entry) {
        const uint16_t original = uint16_t(raw[entry * 2]) |
                                  (uint16_t(raw[entry * 2 + 1]) << 8);
        require(asset.palette[entry] == original, "Palette differs: " + path);
      }
      // Reverse order ensures random-access decode never depends on old pixels.
      for (int f = asset.frameCount - 1; f >= 0; --f) {
        std::fill(frame.begin(), frame.end(), 0x5a);
        require(SageAssets::decodeFrame(id, uint16_t(f), frame.data(), SageAssets::kFrameBytes),
                "Decode failed: " + path);
        require(memcmp(frame.data(), raw.data() + 512 + size_t(f) * SageAssets::kFrameBytes,
                       SageAssets::kFrameBytes) == 0,
                "Frame mismatch: " + path + ":" + std::to_string(f));
        for (size_t n = SageAssets::kFrameBytes; n < frame.size(); ++n)
          require(frame[n] == 0x5a, "Decoder wrote past caller capacity");
        ++frameCount;
      }
      require(!SageAssets::decodeFrame(id, asset.frameCount, frame.data()), "Invalid frame accepted");
      require(SageAssets::frameAt(id, 0) == 0, "Initial timing differs");
      require(SageAssets::frameAt(id, asset.durationMs) == 0, "Loop boundary differs");
      require(SageAssets::frameAt(id, asset.durationMs - 1) == asset.frameCount - 1,
              "Last frame timing differs");
      std::cout << asset.name << ": " << asset.frameCount << " frames + palette exact PASS\n";
    }
    require(!SageAssets::decodeFrame(SageAssets::ClipId::Count, 0, frame.data()), "Invalid ID accepted");
    require(!SageAssets::decodeFrame(SageAssets::ClipId::Calm, 0, nullptr), "Null buffer accepted");
    require(!SageAssets::decodeFrame(SageAssets::ClipId::Calm, 0, frame.data(), SageAssets::kFrameBytes - 1),
            "Short buffer accepted");
    require(frameCount == 804, "Unexpected total frame count");
    std::cout << "PASS: all " << frameCount << " frames, all RGB565 palettes, random access, bounds, timing\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
