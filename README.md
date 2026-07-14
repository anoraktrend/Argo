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

## How it works

1. **LZMA2 compression** with a range-coded, probability-model entropy coder and multithreaded chunked encoding
2. Legacy fallback: **LZ77/LZ78** match-finder for data where LZMA2 would expand
3. **Base85 encoding** of the compressed stream using a C-string-safe alphabet (avoids `"` and `\`)
4. The decoder (`lz` + `b8`) is embedded in the generated C file alongside a threaded file extractor

The generated archive decompresses and extracts files using **multiple threads** via C11 `<threads.h>`.

## Requirements

- A C23 compiler (GCC, Clang) for the compressor
- The generated archive requires C11 `<threads.h>` support and compiles under C++ as well

## Building

```sh
make
```

## Testing

```sh
make test
```

## License

MIT
