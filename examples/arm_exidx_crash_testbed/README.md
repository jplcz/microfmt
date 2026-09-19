# ARM EXIDX Crash Unwind Testbed

A standalone (not built by the main `jplcz_microfmt` CMake project) 32-bit
ARM Linux userspace program that installs a real `SIGSEGV` handler,
deliberately crashes a few stack frames deep, and unwinds the real call
stack using the library's `dl_elf_enumerator_tag` + `arm_exidx_unwinder_tag`
backends against the process's genuine `.ARM.exidx`/`.ARM.extab` tables and
`ucontext_t` register state.

Unlike `examples/exidx_unwinder_test.cpp` and
`examples/chained_unwinder_demo.cpp`, nothing here is simulated: it's a real
cross-compiled ELF binary, run under `qemu-arm` (user-mode emulation), and
can be attached to live with `gdb-multiarch`/VS Code.

## Prerequisites

On Debian/Ubuntu:

```bash
sudo apt install gcc-arm-linux-gnueabi g++-arm-linux-gnueabi qemu-user gdb-multiarch
```

(Substitute `-gnueabihf` packages and
`-DMICROFMT_ARM_TOOLCHAIN_PREFIX=arm-linux-gnueabihf-` if you'd rather use
the hard-float ABI.)

## Building

```bash
cd examples/arm_exidx_crash_testbed
./build.sh
```

(equivalent to `cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=toolchain-arm-linux-gnueabi.cmake && cmake --build build-arm`;
pass extra CMake args through, e.g. `./build.sh -DMICROFMT_ARM_TOOLCHAIN_PREFIX=arm-linux-gnueabihf-`)

This produces `build-arm/arm_exidx_crash_testbed`, a statically linked ARM
binary (so `qemu-arm` needs no target sysroot to run it).

### Testing Thumb support

By default the binary is compiled as 32-bit ARM (A32). Pass
`-DTESTBED_THUMB=ON` to instead compile it as Thumb/Thumb-2 (`-mthumb`),
exercising the R7-as-frame-pointer / `CPSR.T` path
(`arm_abi_traits::resolve_fp_reg`) instead of the default R11/A32 path:

```bash
./build.sh -DTESTBED_THUMB=ON
```

Both configurations should produce an equivalent, fully-resolved
`level_c → level_b → level_a → main → ...` backtrace.

### Complex C++ objects

The crash-path frames (`level_a`/`level_b`/`level_c`) each hold a
non-trivial local object (`frame_context`, with a virtual destructor and
`std::string`/`std::vector`/`std::unique_ptr` members) so the unwinder is
exercised against realistic stack layouts, not just minimal POD frames. The
testbed is built with `-fno-exceptions`: enabling exceptions would give
these functions a real `__gxx_personality_v0` personality routine (an
out-of-line function pointer + LSDA, for exception-driven local cleanup),
which is a different, currently-unsupported EHABI encoding from the
compact-model/generic-bytecode (personality 0/1/2) formats this unwinder
implements.

## Running under qemu-user (no debugger)

```bash
qemu-arm build-arm/arm_exidx_crash_testbed
```

Expected output: a startup banner, then a `CRASH DETECTED` block containing
a real EXIDX-unwound backtrace (`level_c` -> `level_b` -> `level_a` ->
`main`), resolved via `dladdr()`.

## Debugging from VS Code

1. Build as above.
2. Start the emulator paused, waiting for a debugger:
   ```bash
   ./run-under-qemu-gdb.sh
   ```
   (qemu halts the emulated CPU immediately and listens on TCP port 1234
   until a debugger connects — it prints nothing else on its own.)
3. Open this folder (`examples/arm_exidx_crash_testbed`) in VS Code — it has
   its own `.vscode/launch.json` — and run **Attach to qemu-arm (EXIDX
   testbed)** from the Run and Debug view.
4. Set breakpoints (e.g. in `crash_handler`, `level_c`) as usual; VS Code
   drives `gdb-multiarch` over the GDB remote protocol to the paused qemu
   instance.

To debug by hand instead of via VS Code:

```bash
gdb-multiarch build-arm/arm_exidx_crash_testbed \
  -ex "target remote localhost:1234"
```

## Files

- `main.cpp` — the crash/unwind testbed program.
- `CMakeLists.txt` — standalone project (not `add_subdirectory`'d by the
  main build); include path is set directly to `../../include` since
  `jplcz_microfmt` is header-only.
- `toolchain-arm-linux-gnueabi.cmake` — CMake toolchain file selecting the
  `arm-linux-gnueabi-*` cross-compiler.
- `build.sh` — configures and builds `build-arm/` with the cross-toolchain.
- `run-under-qemu-gdb.sh` — starts `qemu-arm -g <port>` against the built
  binary and waits for a debugger to attach.
- `.vscode/launch.json` — VS Code "attach" configuration for the qemu GDB
  stub started by `run-under-qemu-gdb.sh`.
