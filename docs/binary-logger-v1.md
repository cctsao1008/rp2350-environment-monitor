# Environment Binary Logger v1 — experimental implementation (Issue #1)

**Status: Windows ARM cross-compile and image-size guard PASS (user-reported 2026-11-01); actual RP2350 NOR Flash writes, flash endurance, sampling jitter, power-loss recovery and USB Vendor Reset export are NOT validated.**
Default: **OFF**. Existing USB CDC-NCM firmware and `main` remain unchanged.

## Why / scope

The RP2350-PiZero has 520 KiB on-chip SRAM and 16 MiB **external** XIP
QSPI NOR Flash. Data logging uses the last 12 MiB as an opt-in circular
log; the first 4 MiB remain reserved for firmware. No SD card, cloud
database, public HTTP log endpoint or additional I2C reads are introduced.

Log only BME280 (factory-compensated T/RH/P), TSL2591 (CH0/CH1),
LTR390 (ALS/UV raw), SGP40 (SRAW and stateful VOC Index). Exclude
ICM20948/AK09916 from history while leaving live JSON and 1 Hz sensors
unchanged. **These BME280 values are not ADC raw data.**

## Physical layout

- 16 MiB flash mapped at XIP address 0x10000000.
- Firmware reservation: flash offsets `0x000000..0x3fffff` (4 MiB).
- Logger: flash offsets `0x400000..0xffffff`, XIP
  `0x10400000..0x10ffffff` (12 MiB).
- 3072 sectors of 4096 bytes; each sector's first 256-byte page
  is metadata, the remaining 15 pages hold ten records each.
- Exactly **150 samples per sector**, **460800 samples** total;
  theoretical retention **128 hours / 5.33 days** at exactly 1 Hz.
  Earlier-than-necessary sector reclamation and boot-sector gaps reduce it
  slightly. Flash erase/program latency may cause more drops in practice.

Build guard converts the linked ELF to a temporary binary and fails the
build if it exceeds 4 MiB. Do not remove this guard. Flash erase/program
also have runtime checks against crossing this boundary. Physical
protection is **not** guaranteed until actual RP2350 image/boot/partition
behaviour and the picotool update path have been verified.

## Sector metadata — 256 bytes

All integers are explicitly Little Endian; unwritten bytes are 0xff.

| Offset | Type | Meaning |
|---|---|---|
| 0 | 4 ASCII | `ELOG` |
| 4 | u16 | version = 1 |
| 6 | u16 | header size = 256 |
| 8 | u32 | monotonic sector generation (wraps) |
| 12 | u32 | session/boot ID (incremented from newest valid header) |
| 16 | u32 | physical flash partition offset (4 MiB) |
| 20 | u32 | partition length (12 MiB) |
| 24 | u16 | record bytes = 24 |
| 26 | u16 | page bytes = 256 |
| 28 | u16 | sector bytes = 4096 |
| 30 | u16 | samples per page = 10 |
| 32 | u16 | TSL device factor = 408 |
| 34 | u16 | TSL relative factor in 0.001 units = 2700 |
| 36 | u8 | TSL control = 0x10 (100 ms, 25x) |
| 37 | u8 | LTR rate = 0x22 (18-bit/100 ms) |
| 38 | u8 | LTR gain = 0x01 (3x) |
| 39 | u8 | UTC valid = 0; no trusted UTC source |
| 40..251 | bytes | reserved (0xff) |
| 252 | u32 | CRC32 of bytes 0..251 |

Each boot starts a **new sector**, preserving completed pages from previous
boots. The session ID distinguishes repeated monotonic timestamps after
reset. No guessed wall-clock UTC is stored.

## Program page — 256 bytes

| Offset | Type | Meaning |
|---|---|---|
| 0 | u64 | base monotonic time in microseconds (source is manager's millisecond timestamp) |
| 8 | u32 | base sample sequence |
| 12 | u32 | CRC32 of `page[0:12] || page[16:256]` |
| 16..255 | bytes | 10 × 24-byte records |

Unfilled slots are all 0xff, with `delta_ms=0xffff` as the unused
sentinel. Valid offsets are `0..65534`; discontinuities force a partial
page with 0xff padding, with one sample dropped at the transition. A page
is valid only after the entire 256-byte program verifies against its CRC.
A torn page is ignored by the host parser without guessing missing data.

## Record — 24 bytes

| Offset | Type | Meaning |
|---|---|---|
| 0 | u16 | delta milliseconds from page base |
| 2 | i16 | BME280 0.01 C |
| 4 | u16 | BME280 0.01 %RH |
| 6 | u24 | BME280 pressure in Pa |
| 9 | u16 × 2 | TSL2591 CH0, CH1 infrared |
| 13 | u24 × 2 | LTR390 ALS, UV counts |
| 19 | u16 × 2 | SGP40 SRAW, VOC Index |
| 23 | u8 | validity/quality flags |

Quality bits: bit0 BME valid; bit1 TSL valid; bit2 LTR valid; bit3 SGP
valid; bit4 TSL saturated; bit5 VOC Index ready. Bits 6–7 reserved.
A zero reading with the corresponding valid bit set is **not an error**.
Derived Lux, UVI and relative scaling are **not saved per record**:
they are recomputed on the host from raw counts and the sector metadata.
SGP VOC Index *is* persisted because its algorithm has temporal state.

Sequence is reconstructed from `base_sequence + record_index`;
Timestamp is reconstructed as `base_us + delta_ms * 1000`. Time remains
monotonic **within one boot**; do not directly merge timestamps from
different session IDs without an externally supplied UTC anchor.

## Write/recovery safety

- `ENABLE_BINARY_FLASH_LOGGER=OFF` unless deliberately turned ON.
- Only the main-loop idle path writes flash, using
  `flash_safe_execute`; no IRQ, TinyUSB callback, or sensor read may write.
- Only erasable sectors with valid ELOG metadata may be recycled.
  Unknown nonblank sectors are skipped; **never blindly mass-erase**.
- On boot, scan header pages, choose newest generation, start in the next
  available sector. Flash writes are verified after XIP is restored.
- Accumulate ten 24-B samples in SRAM, program one full 256-B page at a
  time. Up to nine not-yet-committed samples can be lost on sudden power
  loss; torn pages are CRC-rejected.
- The Flash erase/program operation temporarily stalls XIP/interrupts.
  It *may* impact USB/TinyUSB and 1 Hz deadlines. This has NOT been
  qualified on target; failures disable logging, not sensor capture.
- Actual Flash wear, unknown regions, firmware updates and RP2350 flash
  partition boot semantics must be verified on real hardware. In
  particular, `picotool load -u` is **not** a substitute for a
  non-overlap/erase-preservation test.

## Host validation (no hardware)

```bash
cc -std=c11 -Wall -Wextra -Werror -Isrc/logger \
  src/logger/envlog_codec.c tests/envlog_codec_test.c \
  -o /tmp/envlog_codec_test
/tmp/envlog_codec_test
python3 -m unittest discover -s tests -p 'test_envlog_format.py' -v
```

## Build (explicit opt-in; NOT automatically flashed)

In an existing configured `build-ncm` with NCM already enabled:

```powershell
git fetch origin
git switch feature/binary-flash-logger
git pull --ff-only
cmake -S . -B build-ncm -DENABLE_BINARY_FLASH_LOGGER=ON
.\scripts\build.ps1 -BuildDir build-ncm
```

**Do not flash this experimental image** until the host checks, RP2350
Flash partition layout, and safely controlled on-target plan are reviewed.
To restore the prior build, configure
`-DENABLE_BINARY_FLASH_LOGGER=OFF` and rebuild.

## Local-only export (existing USB Vendor Reset, not remote HTTP)

**No physical BOOTSEL button is required** when the current firmware
exposes the already-proven USB Vendor Reset interface. Use the same USB
automatic reset mechanism as `scripts/flash.ps1 -Ncm`. `picotool -f`
requests a temporary reboot into the RP2350 Boot ROM's USB PICOBOOT
protocol, reads the selected Flash range, and normally reboots the
device back to the application. No MSC file drag-and-drop is used.

On Windows with the connected NCM image (VID:PID `CAFE:40A3`):

```powershell
$Picotool = "C:\Toolchains\pico-tools\2.3.1\picotool\picotool\picotool.exe"
& $Picotool save -r 0x10400000 0x11000000 `
    envlog-before.bin -t bin --vid 51966 --pid 16547 -f
```

This is a **read-only** dump of the 12 MiB logger partition. The
`-f` path automatically enters and leaves PICOBOOT and is expected to
cause a temporary interruption of USB NCM / HTTP service. Do not
mistake that intentional interruption for a system fault. If there are
multiple matching boards, also specify `--ser` with the actual device
serial. `picotool save -r` requires physical USB access and is not
exposed through the current Cloudflare Tunnel.

```powershell
py tools\envlog_parse.py envlog-before.bin --csv envlog.csv
```

Confirm the dump is exactly 12 MiB. When the logger has not yet been
flashed/enabled, the image is only a preflight backup and will not
contain valid ELOG records. This workflow is source/documentation
verified; the USB `picotool save -r -f` operation has **not yet been
reported as tested on the device**. Retain the existing validated
`scripts/flash.ps1 -BuildDir build-ncm -Ncm` for firmware updates.

Cloudflare Tunnel continues to expose only the existing live sensor
endpoint. No remote binary export API has been added.

## Outstanding Issue #1 acceptance items

- [x] issue, versioned record codec, CRC, metadata and host parser
- [x] opt-in log pipeline + bounded NOR ring + firmware image-size guard
- [x] source-level C and Python regression tests / CI configuration
- [x] **Windows arm-none-eabi-gcc / Pico SDK build verification** (2026-11-01 user log: 140/140; generated image 105192 B < 4 MiB)
- [ ] **RP2350 on-target erase/program timing and USB NCM stability**
- [ ] **actual power-cut tests, wraparound and firmware-update preservation**
- [ ] read-only online local USB streaming export (optional; USB Vendor Reset `picotool save -r -f` export is the initial physical-local solution)
