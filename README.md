# Argo — Self-Extracting C Archives

[![test](https://github.com/anoraktrend/Argo/actions/workflows/test.yml/badge.svg)](https://github.com/anoraktrend/Argo/actions/workflows/test.yml)

Argo is a compression tool that creates **self-extracting C source archives**.
The output is a single `.c` file that decompresses and restores the original files when compiled and run.

The archive file is guaranteed to be **no larger** than the original files combined.

## Usage

```sh
cc -o ccompress compress.c && ./ccompress compress.c Makefile -o Argo.c
cc -o extract Argo.c && ./extract
```

The compressor (`ccompress`) reads any number of input files and produces `Argo.c`.
The resulting C file is compiled and executed to reproduce the original files.

Recommended flags for the generated archive (produces smaller binaries):

```sh
cc -Os -ffast-math -march=native -lpthread Argo.c -o extract && ./extract
```

On MSVC:

```
cl /O1 /fp:fast Argo.c /Fe:extract.exe
```

## How it works

1. **LZMA2 compression** with a range-coded, probability-model entropy coder and multithreaded chunked encoding
2. Legacy fallback: **LZ77/LZ78** match-finder for data where LZMA2 would expand
3. **Base85 encoding** of the compressed stream using a C-string-safe alphabet (avoids `"` and `\`)
4. The decoder (`lz` + `b8`) is embedded in the generated C file alongside a threaded file extractor

The generated archive decompresses and extracts files using **POSIX threads** (pthreads) on Linux/macOS/MinGW, and single-threaded extraction on MSVC.

## Requirements

- A C23 compiler (GCC, Clang) for the compressor
- The generated archive compiles with any C99-or-later compiler (GCC, Clang, MSVC, MinGW) on Linux, macOS, or Windows

## Building

```sh
make
```

## Testing

```sh
make test
```

Or run the full test suite:

```sh
test/run.sh              # Linux, macOS, MinGW
pwsh test/run.ps1        # MSVC on Windows
```

The CI pipeline (`test.yml`) runs tests across 5 environments: Linux (glibc), Alpine Linux (musl), macOS, MinGW (msys2), and MSVC.

## License

MIT — see [LICENSE](LICENSE) for details.
