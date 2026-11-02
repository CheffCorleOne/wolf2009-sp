// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_DDS_H
#define WOLF2009_DDS_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace wolf2009 {
struct DdsLimits {
    std::size_t sourceBytes = 64u * 1024u * 1024u;
    std::size_t decodedBytes = 256u * 1024u * 1024u;
    std::uint32_t maximumWidth = 8192;
    std::uint32_t maximumHeight = 8192;
};
struct DdsImage {
    std::uint32_t width = 0, height = 0, mipCount = 1;
    std::string format; // BC1/DXT1, BC2/DXT3, BC3/DXT5 or A8.
    std::uint32_t sourceFormatMarker = 0; // Original dwRGBBitCount (sometimes a game-specific marker).
    std::vector<unsigned char> rgba; // Mip zero, row-major top to bottom, RGBA8.
};
// Bounded legacy 2D DDS reader. The complete tightly packed declared mip chain
// must be present; only mip zero is decoded. Standard BC1/2/3 color and alpha
// are returned without color-space conversion, normal-map swizzle, or material
// semantics. A8 returns white RGB plus the stored alpha. DX10/cube/volume and
// unobserved RGB-mask/premultiplied formats are rejected explicitly.
DdsImage DecodeDdsMip0(const std::vector<unsigned char>& data,
                       const DdsLimits& limits = DdsLimits());
int RunDdsTests();
}
#endif
