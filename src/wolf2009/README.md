# Wolfenstein (2009) resource-container probe

Independent C++11 read-only implementation of the SPK/MPK **container**, built
separately from the Android engine. It does not implement maps, rendering,
Havok, Scaleform, AI, scripts, or gameplay. Native game support is not claimed.
No original game data is included or written by this tool.

The observed layout is little-endian version `300`, followed by chunks with
reversed FourCC, inflated size, compressed size, and a zlib-wrapped stream.
Each inflated chunk starts with a file count and contiguous records containing
a NUL-terminated name and two matching 32-bit lengths. The table and each
payload end at a four-byte boundary. Unknown printable FourCC values remain
visible as metadata; they do not gain a content decoder.

`Archive::Scan` validates chunk streams, tables, payload spans and alignment,
and retains metadata only. `ReadEntry` decompresses one indexed chunk again
and returns an owned byte vector; the caller owns and keeps its stream open.
The parser never uses entry names as host paths. Default limits: 64 MiB
compressed / 256 MiB inflated per chunk, 4,096 chunks, 100,000 total entries,
4,096 bytes per NUL-terminated name, and 64 MiB total name characters. Inputs
outside these limits are rejected explicitly; limits are configurable.

## Build and verify

With a C++11 compiler, CMake and zlib development package:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/wolf2009-inspect --self-test
./build/wolf2009-inspect /path/to/your/game/SP/base/some-file.spk
```

For upstream engine's bundled miniz, use its absolute directory:

```sh
cmake -S . -B build -DWOLF2009_MINIZ_SOURCE_DIR=/path/to/engine/doom3/neo/externlibs/miniz
```

On Windows a multi-configuration generator may place the executable under
`build/Debug/` or `build/Release/`. Android NDK builds can use the same standalone
CMake target with the NDK toolchain and `ANDROID_ABI=arm64-v8a`; this does not
create an APK or prove runtime support. The code has no OS or graphics API calls.
The CLI accepts multiple archives and returns nonzero if any fails validation.
It reads only; filenames in terminal output are escaped.

Synthetic self-tests cover multi-file/multi-chunk reads, unknown chunk types,
empty files, invalid headers and indices, truncated input, corrupt zlib, size
mismatches, malformed names and tables, payload overrun, trailing bytes, changed
indexed metadata, and configured resource limits.

## Evidence and provenance

Format investigation used local original archive bytes and the public format
description in [Wolfenstein-SPK-Tool](https://github.com/dortkoldantaciz/Wolfenstein-SPK-Tool).
This parser is a separate bounded implementation; no community extractor code
or proprietary engine/game implementation is included. New files carry
`GPL-3.0-or-later`; linking the existing engine or third-party compression code
retains their own notices and terms.

The project is currently a development probe: no engine VFS registration is
performed by this standalone target. Content families such as `MODL`, `PROC`,
`HKXS`, `SGFX`, `BRAI` require their own documented decoders and replacements.
