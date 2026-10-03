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

### Running on target under QEMU

Without a host compiler, the same file runs on the target architectures under the QEMU shipped with
ESP-IDF. Create a throwaway ESP-IDF project outside the repository whose `main` component compiles a
small `wrapper.cpp` (an `app_main()` that calls `dc_blocker_test_main()` and prints its result) together
with `test/host/dc_blocker_test.cpp`, adds `components/audio/audio_engine/include` to `INCLUDE_DIRS`,
and sets `COMPILE_DEFINITIONS "main=dc_blocker_test_main"` on the test file with
`set_source_files_properties()`. Then run `idf.py set-target esp32s3 build` (or `esp32c3` for RISC-V)
and `idf.py qemu`, with the ESP-IDF QEMU binaries (`~/.espressif/tools/qemu-xtensa` or `qemu-riscv32`)
on `PATH`.
