# Argo v1.0.0 Release Notes

**Release Date:** July 9, 2026

## Overview
Argo v1.0.0 marks the official launch of a powerful tool for generating self-extracting compressed file archives in portable C code. This initial release provides a solid foundation for creating compact, distributable archives that can decompress themselves without external dependencies.

## Features
- **Self-Extracting Archives**: Generate C code that embeds and decompresses files on execution
- **Portable Output**: Creates standalone C source files compatible across multiple platforms
- **Compression Support**: Efficient compression algorithms for reduced output size
- **Cross-Platform CI**: Automated testing across Linux, macOS, and Windows environments

## What's Included
- Core compression engine (`compress.c`)
- Makefile for easy compilation
- Multi-platform GitHub Actions workflows
- Clean, maintainable C codebase

## Getting Started
```bash
make
./argo [input_file] [output_file]
```

## Platform Support
- Linux (gcc/clang)
- macOS (clang)
- Windows (MSVC/MinGW)

## Known Limitations
- Initial release focuses on core compression functionality
- Enhanced features and optimizations planned for future versions

## Future Roadmap
- Performance optimizations
- Additional compression algorithms
- Extended file format support
- Comprehensive documentation and examples

## Contributors
- Lucy Ada Randall (@anoraktrend)

---

**Argo v1.0.0** is production-ready and welcomes community feedback and contributions!
