#include "gfx.h"

#include "font.h"

#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/present.h"

#include <math.h>
#include <string.h>

enum
{
    kFrontBuffers = 2,
    kSwapInterval = 1,
    kPresentTimeoutMs = 1000,
};

void Canvas::Clear(u32 color)
{
    for (size_t i = 0; i < m_pixels.size(); i++)
        m_pixels[i] = color;
}

void Canvas::SetClip(int x, int y, int w, int h)
{
    m_clipX0 = x < 0 ? 0 : x;
    m_clipY0 = y < 0 ? 0 : y;
    m_clipX1 = x + w > m_width ? m_width : x + w;
    m_clipY1 = y + h > m_height ? m_height : y + h;
}

void Canvas::ResetClip()
{
    SetClip(0, 0, m_width, m_height);
}

void Canvas::FillRect(int x, int y, int w, int h, u32 color)
{
    int x0 = x < m_clipX0 ? m_clipX0 : x;
    int y0 = y < m_clipY0 ? m_clipY0 : y;
    int x1 = x + w > m_clipX1 ? m_clipX1 : x + w;
    int y1 = y + h > m_clipY1 ? m_clipY1 : y + h;
    if ((color >> 24) != 0xFF)
    {
        for (int row = y0; row < y1; row++)
            for (int col = x0; col < x1; col++)
                Blend(col, row, color);
        return;
    }
    for (int row = y0; row < y1; row++)
    {
        u32* line = &m_pixels[row * m_width];
        for (int col = x0; col < x1; col++)
            line[col] = color;
    }
}

void Canvas::FillRoundRect(int x, int y, int w, int h, int radius, u32 color)
{
    if (w <= 0 || h <= 0)
        return;
    int r = radius;
    if (2 * r > w) r = w / 2;
    if (2 * r > h) r = h / 2;
    // The straight middle part, then the rows with corners.
    FillRect(x, y + r, w, h - 2 * r, color);
    u32 alpha = color >> 24;
    for (int row = 0; row < r; row++)
    {
        // Distance of the pixel centre below the corner circles' centres, as seen from the edge.
        float dy = r - row - 0.5f;
        for (int col = 0; col < w; col++)
        {
            float dx = col < r ? r - col - 0.5f : (col >= w - r ? col - (w - r) + 0.5f : 0.0f);
            u32 a = alpha;
            if (dx > 0)
            {
                float cover = r + 0.5f - sqrtf(dx * dx + dy * dy);
                if (cover <= 0)
                    continue;
                if (cover < 1)
                    a = (u32)(alpha * cover);
            }
            u32 pixel = a << 24 | (color & 0xFFFFFF);
            Blend(x + col, y + row, pixel);
            Blend(x + col, y + h - 1 - row, pixel);
        }
    }
}

void Canvas::FillCircle(int centerX, int centerY, int radius, u32 color)
{
    FillRoundRect(centerX - radius, centerY - radius, 2 * radius, 2 * radius, radius, color);
}

void Canvas::FillGradient(int x, int y, int w, int h, u32 top, u32 bottom)
{
    for (int row = 0; row < h; row++)
    {
        u32 color = 0xFF000000;
        for (int shift = 0; shift < 24; shift += 8)
        {
            int a = (int)((top >> shift) & 0xFF);
            int b = (int)((bottom >> shift) & 0xFF);
            color |= (u32)(a + (b - a) * row / (h > 1 ? h - 1 : 1)) << shift;
        }
        FillRect(x, y + row, w, 1, color);
    }
}

void Canvas::FrameRect(int x, int y, int w, int h, int thickness, u32 color)
{
    FillRect(x, y, w, thickness, color);
    FillRect(x, y + h - thickness, w, thickness, color);
    FillRect(x, y, thickness, h, color);
    FillRect(x + w - thickness, y, thickness, h, color);
}

int Canvas::TextWidth(const std::string& text, int scale)
{
    return (int)text.size() * kFontCellW * scale;
}

void Canvas::DrawText(int x, int y, const std::string& text, u32 color, int scale, int maxWidth)
{
    std::string shown = text;
    if (maxWidth > 0 && TextWidth(shown, scale) > maxWidth)
    {
        size_t fit = (size_t)(maxWidth / (kFontCellW * scale));
        shown = fit > 3 ? shown.substr(0, fit - 3) + "..." : shown.substr(0, fit);
    }
    for (size_t i = 0; i < shown.size(); i++, x += kFontCellW * scale)
    {
        int ch = (unsigned char)shown[i];
        if (ch < kFontFirstChar || ch > kFontLastChar)
            ch = '?';
        const unsigned short* glyph = g_font[ch - kFontFirstChar];
        for (int gy = 0; gy < kFontCellH; gy++)
        {
            unsigned short bits = glyph[gy];
            for (int gx = 0; bits && gx < kFontCellW; gx++)
                if (bits & (0x8000 >> gx))
                    FillRect(x + gx * scale, y + gy * scale, scale, scale, color);
        }
    }
}

void Canvas::DrawTextCentered(int y, const std::string& text, u32 color, int scale)
{
    DrawText((m_width - TextWidth(text, scale)) / 2, y, text, color, scale);
}

// Alpha-blends one pixel inside the clip rectangle.
void Canvas::Blend(int x, int y, u32 src)
{
    if (x < m_clipX0 || x >= m_clipX1 || y < m_clipY0 || y >= m_clipY1)
        return;
    u32 a = src >> 24;
    u32& dst = m_pixels[y * m_width + x];
    if (a == 0xFF)
    {
        dst = src;
        return;
    }
    u32 r = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * (255 - a)) / 255;
    u32 g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * (255 - a)) / 255;
    u32 b = ((src & 0xFF) * a + (dst & 0xFF) * (255 - a)) / 255;
    dst = 0xFF000000 | r << 16 | g << 8 | b;
}

void Canvas::DrawImage(int x, int y, int w, int h, const u32* pixels)
{
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            Blend(x + col, y + row, pixels[row * w + col]);
}

void Canvas::DrawImageScaled(int x, int y, int w, int h, const u32* pixels, int sw, int sh)
{
    for (int row = 0; row < h; row++)
    {
        // Source position in 1/256 pixels, sampled at pixel centres.
        int sy = ((row * 2 + 1) * sh * 128) / h - 128;
        sy = sy < 0 ? 0 : sy;
        int y0 = sy >> 8;
        int y1 = y0 + 1 < sh ? y0 + 1 : y0;
        u32 fy = (u32)(sy & 0xFF);
        for (int col = 0; col < w; col++)
        {
            int sx = ((col * 2 + 1) * sw * 128) / w - 128;
            sx = sx < 0 ? 0 : sx;
            int x0 = sx >> 8;
            int x1 = x0 + 1 < sw ? x0 + 1 : x0;
            u32 fx = (u32)(sx & 0xFF);
            u32 p00 = pixels[y0 * sw + x0], p01 = pixels[y0 * sw + x1];
            u32 p10 = pixels[y1 * sw + x0], p11 = pixels[y1 * sw + x1];
            u32 out = 0;
            for (int shift = 0; shift < 32; shift += 8)
            {
                u32 top = ((p00 >> shift) & 0xFF) * (256 - fx) + ((p01 >> shift) & 0xFF) * fx;
                u32 bottom = ((p10 >> shift) & 0xFF) * (256 - fx) + ((p11 >> shift) & 0xFF) * fx;
                out |= ((top * (256 - fy) + bottom * fy) >> 16) << shift;
            }
            Blend(x + col, y + row, out);
        }
    }
}

bool Screen::Open(int* status)
{
    uint32_t width = 0;
    uint32_t height = 0;
    NTSTATUS s = xsdk_gpu_open();
    if (NT_SUCCESS(s))
        s = xsdk_present_open(XSDK_DISPLAY_720P, kFrontBuffers, kSwapInterval, &width, &height);
    *status = (int)s;
    if (!NT_SUCCESS(s))
        return false;
    m_canvas.m_width = (int)width;
    m_canvas.m_height = (int)height;
    m_canvas.m_pixels.assign(width * height, 0xFF000000);
    m_canvas.ResetClip();
    m_version = 1;
    m_bufferVersion[0] = m_bufferVersion[1] = 0;
    return true;
}

// tiled_index comes from libxenon:drivers/console/console.c:60-61 (as xsdk's
// tests/tiled_index.h carries it), under this notice:
//
// Copyright 2009  Georg Lukas <georg@op-co.de>
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//
// A pixel's index in the front buffer's 32x32 tiles.
static inline u32 TiledIndex(u32 x, u32 y, u32 width)
{
    return ((y >> 5) * 32 * width + ((x >> 5) << 10) + (x & 3) + ((y & 1) << 2) + (((x & 31) >> 2) << 3) +
            (((y & 31) >> 1) << 6)) ^
        ((y & 8) << 2);
}

void Screen::Present()
{
    uint32_t address = 0;
    if (!NT_SUCCESS(xsdk_present_begin(&address, kPresentTimeoutMs)))
        return;
    for (u32 i = 0; i < kFrontBuffers; i++)
    {
        if (xsdk_present_buffer_address(i) != address || m_bufferVersion[i] == m_version)
            continue;
        // The front buffer stores each pixel's bytes B, G, R, A, and is
        // write-combined memory: volatile 32-bit stores only.
        volatile uint32_t* front = xsdk_present_pixels(address);
        const Canvas& c = m_canvas;
        for (int y = 0; y < c.m_height; y++)
        {
            const u32* line = &c.m_pixels[y * c.m_width];
            for (int x = 0; x < c.m_width; x++)
                front[TiledIndex(x, y, c.m_width)] = __builtin_bswap32(line[x]);
        }
#if defined(__powerpc__)
        __asm__ volatile("sync" ::: "memory");
#endif
        m_bufferVersion[i] = m_version;
    }
    xsdk_present_end();
}
