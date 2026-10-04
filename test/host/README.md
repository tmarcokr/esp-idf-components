# Host tests

Small standalone checks for header-only components. They need no ESP-IDF: each test is a single
`.cpp` file with its own `main()` that prints every failed check and returns 1 on failure.

## Running the tests

`run_host_tests.sh` builds and runs every `test/host/*.cpp` (new tests are picked up automatically)
with `-std=c++20 -Wall -Wextra -Werror` plus AddressSanitizer and UndefinedBehaviorSanitizer, and
exits 1 if any test fails to build or run, or if no test is found. CI runs it on every pull request
(job "Host tests"). From any directory:

```bash
test/host/run_host_tests.sh
```

- `CXX` selects the compiler (default `g++`, GCC 13 or newer). It must name a single executable
  (e.g. `CXX=clang++`); a command with arguments such as `CXX="ccache g++"` is not supported.
- `HOST_TEST_CXXFLAGS` replaces the optimization and sanitizer flags, e.g.
  `HOST_TEST_CXXFLAGS=-O2 test/host/run_host_tests.sh`.
- Every `components/**/include` directory is on the include path. A host test may only include
  headers that need no ESP-IDF header.

Without a host compiler, run it in a GCC container from the repository root:

```bash
podman run --rm -v "$PWD":/src:Z -w /src docker.io/library/gcc:13 bash test/host/run_host_tests.sh
```

## DC blocker (`dc_blocker_test.cpp`)

Checks that `DcBlocker` decays to exactly 0 after impulses, DC steps and full-scale input for every
cutoff preset, matches a truncating `/ 32768` reference, and keeps the 1 kHz response within 1 % of the
previous implementation. The last line of the output is `dc_blocker_test: PASS`.

## Compressor (`compressor_test.cpp`)

Pins the output of the float-root compressor (`BasicDynamicRangeCompressor<EnvelopeRoot::Float>`,
the one used on targets with an FPU) to a verbatim copy of its original 64-bit division: every volume
in `{0, 1, 100, 600, 800, 1000, 2000, 13107, 65535}` times a sine, noise, square and burst signal at
amplitudes from 100 to 2^22 - 1 (44 100 samples each), inputs on both sides of the `v * volume`
32-bit overflow boundary, and volume changes every 1000 samples must produce identical samples
(zero mismatches).

It then checks the integer root used on targets without an FPU: `squareRootFloor()` must equal
`floor(sqrt(x))` for every `x < 2^22`, around every perfect square, at `UINT32_MAX` and on 10^7
pseudo-random values, and must match the float root within 1 (exactly below 2^20). The integer-root
compressor runs the same signal suite against the reference with at most 16 LSB of error per sample
and 0.5 LSB RMS. The host build uses the float root as `DynamicRangeCompressor` (`native root:
float`); both instantiations are tested regardless. The last line of the output is
`compressor_test: PASS`.

## Ring geometry (`ring_geometry_test.cpp`)

Checks `RingGeometry`: the 16384-sample geometry (watermark 8192, chunk 4096, prefills 4096 and
16383), `isValidSize()`, and for N in `{8, 16, 32, 2048}` every `(write, read)` index pair against the
branchy `available` formula with `available + freeSpace == N - 1`. It then simulates a producer
(mirroring `refillBuffer()`) and a 256-sample mixer cycle for N in `{2048, 4096, 16384, 65536}`, SD
and memory-backed prefills: FIFO order, no overwrite of unread data, and no underrun while a refill
runs every mixer cycle. The last line of the output is `ring_geometry_test: PASS`.

## Ring memory (`ring_memory_test.cpp`)

Checks `resolveRingPlacement()`: `RingMemory::Auto` with and without PSRAM, forced `Psram` and
`Internal`, the defaults (16384 samples in PSRAM, 4096 in internal RAM), accepted and rejected
sizes, unknown memory values, and every size from 1 to 131072 for each memory (accepted only when it
is a power of two from 2048 to 65536).
The last line of the output is `ring_memory_test: PASS`.

## WAV data size (`wav_data_size_test.cpp`)

Checks `clampWavDataSize()`, which clamps the data size declared by a WAV header to the bytes the
file really holds after the data offset, rounded down to whole 16-bit samples: truncated files, the
`0xFFFFFFFF` streaming placeholder, odd sizes, a data offset at or beyond the end of the file, files
larger than 4 GiB and the maximal offset. It includes the private header
`components/audio/audio_channel/WavDataSize.hpp` by relative path. The last line of the output is
`wav_data_size_test: PASS`.

## Running on target under QEMU

Without a host compiler, the same file runs on the target architectures under the QEMU shipped with
ESP-IDF. Create a throwaway ESP-IDF project outside the repository whose `main` component compiles a
small `wrapper.cpp` (an `app_main()` that calls `<name>_main()` and prints its result) together
with `test/host/<name>.cpp`, adds `components/audio/audio_engine/include` to `INCLUDE_DIRS`,
and sets `COMPILE_DEFINITIONS "main=<name>_main"` on the test file with
`set_source_files_properties()` (for example `dc_blocker_test_main`, `compressor_test_main`).
Disable the task watchdog (`CONFIG_ESP_TASK_WDT_EN=n`): the compressor test runs for several
seconds without yielding. Then run `idf.py set-target esp32s3 build` (or `esp32c3` for RISC-V)
and `idf.py qemu`, with the ESP-IDF QEMU binaries (`~/.espressif/tools/qemu-xtensa` or `qemu-riscv32`)
on `PATH`.
