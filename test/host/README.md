# Host tests

Small standalone checks for header-only components. They need no ESP-IDF: each test is a single
`.cpp` file with its own `main()` that prints every failed check and returns 1 on failure.

## DC blocker (`dc_blocker_test.cpp`)

Checks that `DcBlocker` decays to exactly 0 after impulses, DC steps and full-scale input for every
cutoff preset, matches a truncating `/ 32768` reference, and keeps the 1 kHz response within 1 % of the
previous implementation.

Build and run with a host compiler, from the repository root:

```bash
g++ -std=c++20 -Wall -Wextra -I components/audio/audio_engine/include test/host/dc_blocker_test.cpp -o /tmp/dcb && /tmp/dcb
```

The last line of the output is `dc_blocker_test: PASS` and the exit code is 0 when every check passes.

## Compressor (`compressor_test.cpp`)

Pins the output of `DynamicRangeCompressor` to a verbatim copy of its original 64-bit division:
every volume in `{0, 1, 100, 600, 800, 1000, 2000, 13107, 65535}` times a sine, noise, square and
burst signal at amplitudes from 100 to 2^22 - 1 (44 100 samples each), inputs on both sides of the
`v * volume` 32-bit overflow boundary, and volume changes every 1000 samples must produce identical
samples (zero mismatches). Built and run like the DC blocker test:

```bash
g++ -std=c++20 -Wall -Wextra -I components/audio/audio_engine/include test/host/compressor_test.cpp -o /tmp/drc && /tmp/drc
```

The last line of the output is `compressor_test: PASS`.

### Running on target under QEMU

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
