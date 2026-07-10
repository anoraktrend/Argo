# Argo — Self-Extracting C Archives

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

## How it works

1. **LZ77 compression** with a brute-force match finder scanning an 8KB window
2. **Base85 encoding** of the compressed stream using a C-string-safe alphabet (avoids `"` and `\`)
3. The decoder (`lz` + `b8`) is embedded in the generated C file

## Requirements

- A C99 compiler (GCC, Clang, MSVC, TinyCC)
- The generated archive is valid C++ for interpreter support (Cling, Cint)

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
