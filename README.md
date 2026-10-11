# Tretro

Play any Libretro core directly in a terminal.

Tretro is an ultra-lightweight, high-performance terminal frontend for Libretro emulator cores, powered by a custom terminal engine.

## Features

- **Terminal Rendering**: High-speed console rendering with ANSI true-color and glyph optimizations.
- **Core Downloader**: In-app libretro core download and update manager.
- **Auto-Detection**: Automatic ROM and core association.
- **Ultra-Compact Binary**: Highly optimized build pipeline with section garbage collection and custom packing.
- **Audio & Gamepad**: Multi-channel audio resampler and controller support.

## Prerequisites

- [xmake](https://xmake.io/) (>= 2.7.0)
- A C++17 compatible compiler (MinGW-w64 / GCC / Clang / MSVC)
- Python 3 (optional, for post-build packing and binary size analysis)

## Building

### 1. Clone with Submodules
```bash
git clone --recursive https://github.com/TheAdrianPOP/Tretro.git
cd Tretro
```
### 2. Configure & Build
** Linux(not supported/won't compile)**

**Windows (MinGW/UCRT64):**
```bash
xmake f --plat=windows --toolchain=mingw -m release
xmake
```
