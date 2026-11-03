// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOLF2009_ENGINE_OFFSCREEN_FRAME_H
#define WOLF2009_ENGINE_OFFSCREEN_FRAME_H
// Test-only C++98 boundary. Uses the initialized native EGL renderer and the
// same readback/BMP implementation as the world integration checks.
class idRenderWorld;
struct renderView_s;
enum { Wolf2009_QAFrameWidth = 512, Wolf2009_QAFrameHeight = 512,
       Wolf2009_QAFrameBytes = Wolf2009_QAFrameWidth * Wolf2009_QAFrameHeight * 4 };
bool Wolf2009_RenderQaScene(idRenderWorld*, const renderView_s&, unsigned char* rgba, int bytes);
bool Wolf2009_WriteQaFrame(const char* relativePath, const unsigned char* rgba, int bytes);
#endif
