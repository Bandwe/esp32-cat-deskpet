# Third-party components and assets

This public repository does not apply a single license to all its contents.
Project-authored material has no project-wide open-source license grant at this
time. Third-party material remains under its original licenses.

## Firmware dependencies (downloaded separately)

The build uses the following libraries distributed in
[Waveshare ESP32-S3-Touch-LCD-1.69](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.69),
at commit `25fed2f7e8411f2522448b58906774db99a14a08`:

- GFX_Library_for_Arduino 1.6.5. Preserve its `license.txt` (Adafruit BSD notice),
  source copyright notices and font-specific notices.
- SensorLib 0.4.1. Preserve `LICENSE` (MIT), `THIRD_PARTY_NOTICES.md`, and nested
  Bosch BSD-3-Clause licenses and source notices.
- The Waveshare repository's root `LICENSE` is Apache-2.0. It does not replace
  the individual bundled libraries' licenses.

The dependency-fetch script retains the two libraries' contents and notices;
their downloaded copies are ignored by Git. Arduino-ESP32 3.3.11 and its
toolchain are installed separately using Arduino CLI and retain their licenses.

## Fonts and certificates

- `server/fonts/NotoSansSC-Regular.otf`: Noto Sans CJK SC, SIL Open Font License
  1.1. The license is included as `server/fonts/OFL.txt`.
- `sketch/DeskPetV01/MenuLabels.h` contains fixed rendered menu labels, with its
  original font attribution in the header. No Microsoft font file is bundled.
- `sketch/DeskPetV01/IsrgRootX1.h` and `tools/tests/isrgrootx1_r18.pem` contain
  the public ISRG Root X1 trust certificate, not a private key.

## Enclosure reference

The enclosure was modeled against Waveshare's official 2024-06-06 STEP board
model. That reference file is not redistributed here. See the enclosure README
for the official download. This is an independent case design, not an official
Waveshare accessory or fit guarantee.

Python dependencies are installed from their pinned requirements files and
retain their respective upstream licenses.
