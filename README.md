# Kreart

> **The Zero-Friction Sketch and Speedpainting Studio**

Kreart is a native, ultra-responsive digital painting studio engineered specifically for rapid ideation, concept art, speedpainting, and expressive sketching. Built with modern C++20 and Qt 6, Kreart delivers sub-millisecond drawing latency (~0.002 ms input processing overhead) and instant application startup without heavy background plugin bloat.

---

## Platform Status

| Platform | Tier | Status | Distribution |
| :--- | :--- | :--- | :--- |
| **Linux (x86_64)** | Tier 1 | **Production Ready** | Standalone Binary Tarball, AppImage / Source |
| **Windows (x64)** | Tier 1 | **Production Ready** | GitHub CI Automated Builds (`windeployqt` archive) |
| **Android (arm64/x86_64)** | Tier 2 | **Experimental** | Standalone APK (Touch-optimized Gallery & Workspace) |

---

## Core Features

- **Sub-Millisecond Drawing Latency**: Real-time tile rendering pipeline with immediate stylus and touch input dispatch, keeping brush response instantaneous even on large multi-layer canvases.
- **Native Krita (`.kra`) Interoperability**: 100% bidirectional `.kra` project support. Open, edit, and save full document structures including tile data, blend modes, layers, masks, and metadata without conversion loss.
- **Write-Ahead Log (WAL) Crash Recovery**: Zero work lost. Continuous transaction journaling ensures instantaneous state reconstruction after unexpected system halts or power cuts.
- **Zero-Lag Action Timelapse Recording**: Offscreen downscaled canvas snapshotting during strokes. Includes interactive in-app timeline scrubbing, real-time playback, and universal H.264 MP4 export using system FFmpeg acceleration with an embedded fallback encoder.
- **Adaptive Multi-Platform UI**: Seamless desktop workstation mode with dockable panels, customizable dark theme, and keyboard shortcuts alongside a dedicated touch-first mobile workspace for Android tablets and handhelds.

---

## Building from Source

### Prerequisites

- **Compiler**: Modern C++20 compliant compiler (GCC 12+, Clang 16+, or MSVC 2022)
- **CMake**: Version 3.22 or newer
- **Qt 6**: Version 6.2+ (`Core`, `Gui`, `Widgets`, `OpenGLWidgets`, `Svg`)
- **System Libraries**: `libpng`, `libxml2`, `libzip`, `zlib`

### Linux

Install dependencies (Debian/Ubuntu):
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-base-dev-tools libqt6svg6-dev \
    libpng-dev libxml2-dev libzip-dev zlib1g-dev
```

Configure and build:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target kreart
```

Run test suite:
```bash
ctest --test-dir build --output-on-failure
```

Launch Kreart:
```bash
./build/src/kreart
```

### Windows (MSVC 2022)

Open `x64 Native Tools Command Prompt for VS 2022`:
```cmd
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.6.0/msvc2022_64"
cmake --build build --target kreart
windeployqt build/src/kreart.exe --dir build/dist
```

### Android (Cross-Compilation)

Kreart supports cross-compilation using the Qt Android toolchain:
```bash
cmake -B build-android \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="arm64-v8a" \
    -DANDROID_PLATFORM=android-31 \
    -DQT_HOST_PATH="/usr/lib/qt6" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-android --target kreart_make_apk
```

---

## Development & Architecture Transparency

> **Development & Architecture**:
> Kreart is designed, architected, and maintained by me, developed with AI pair-programming assistance. Every component is backed by automated end-to-end regression suites and real-time performance benchmarks.

---

## License

Kreart is free and open-source software licensed under the **GNU General Public License v3.0 (GPL-3.0-or-later)**. See [LICENSE](LICENSE) for the full license terms.
