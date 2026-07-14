Argo v1.4.0 — 2026-07-14

### Major

- **Windows/MSVC support** — full CI pipeline compiling with MSVC on Windows. The
  generated decoder uses a Win32 pthread shim (CreateThread) on MSVC with
  single-threaded extraction, and POSIX threads on Linux/macOS/MinGW.
- **Cross-platform CI** — 5 environments: Linux (glibc), Alpine (musl), macOS,
  MinGW (msys2), and MSVC (Windows).
- **MIT licensed** — SPDX headers on all source files.

### Robustness

- OOM checks on all allocations (ms_new, lzma_encode, lzma2_encode)
- Large file support via fseeko/ftello (_fseeki64 on MSVC)
- strerror(errno) on file open/read failures
- Write errors detected on fclose
- Generated decoder now bounds-checks LZMA2 decompression output
- Corrupted input detection: rc_init return value checked
- x86 filter now receives actual decompressed size (was using compressed size)

### Bug fixes

- MSVC round-trip: .exe extension now explicit in /Fe and run commands
- Dead #ifdef _MSC_VER block removed from pthread decoder path
- MSVC C2065: 'o' declared inside MSVC block (was before #ifdef)
- Generated decoder mkdepth fixed for Windows path segments
