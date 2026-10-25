#include "gfx.h"

#include "font.h"

#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"

#include <math.h>
#include <string.h>

enum
{
    kFrontBuffers = 2,
    kSwapInterval = 1,
    kPresentTimeoutMs = 1000,
    kCanvasWidth = 1280,
    kCanvasHeight = 720,
    // Share of an SD picture used, in percent: SD TVs cut off more of the
    // edges than HD ones, which the layout's margins are made for.
    kSdSafePercent = 90,
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

// `src` over `dst` by src's alpha. (v + 1 + (v >> 8)) >> 8 is v / 255 for every
// v this can produce, without the division (slow on the console's CPU).
static inline u32 Mix(u32 dst, u32 src)
{
    u32 a = src >> 24;
    if (a == 0xFF)
        return src;
    if (a == 0)
        return dst;
    u32 ia = 255 - a;
    u32 r = ((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia;
    u32 g = ((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * ia;
    u32 b = (src & 0xFF) * a + (dst & 0xFF) * ia;
    r = (r + 1 + (r >> 8)) >> 8;
    g = (g + 1 + (g >> 8)) >> 8;
    b = (b + 1 + (b >> 8)) >> 8;
    return 0xFF000000 | r << 16 | g << 8 | b;
}

void Canvas::FillRect(int x, int y, int w, int h, u32 color)
{
    int x0 = x < m_clipX0 ? m_clipX0 : x;
    int y0 = y < m_clipY0 ? m_clipY0 : y;
    int x1 = x + w > m_clipX1 ? m_clipX1 : x + w;
    int y1 = y + h > m_clipY1 ? m_clipY1 : y + h;
    if ((color >> 24) == 0)
        return;
    if (color == 0x80000000)
    {
        // Half-transparent black (the shade behind dialogs): halve each channel.
        for (int row = y0; row < y1; row++)
        {
            u32* line = &m_pixels[row * m_width];
            for (int col = x0; col < x1; col++)
                line[col] = 0xFF000000 | ((line[col] >> 1) & 0x7F7F7F);
        }
        return;
    }
    if ((color >> 24) != 0xFF)
    {
        for (int row = y0; row < y1; row++)
        {
            u32* line = &m_pixels[row * m_width];
            for (int col = x0; col < x1; col++)
                line[col] = Mix(line[col], color);
        }
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
    // The straight middle part, then the rows with corners. Coverage only grows
    // from a row's ends inwards, so only the few edge pixels at each end are
    // worked out one by one; from the first fully covered one it's a plain fill.
    FillRect(x, y + r, w, h - 2 * r, color);
    u32 alpha = color >> 24;
    for (int row = 0; row < r; row++)
    {
        int top = y + row;
        int bottom = y + h - 1 - row;
        // Distance of the pixel centre from the corner circles' centres, as seen from the edge.
        float dy = r - row - 0.5f;
        int col = 0;
        for (; col < r; col++)
        {
            float dx = r - col - 0.5f;
            float cover = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cover >= 1)
                break;
            if (cover <= 0)
                continue;
            u32 pixel = (u32)(alpha * cover) << 24 | (color & 0xFFFFFF);
            Blend(x + col, top, pixel);
            Blend(x + w - 1 - col, top, pixel);
            Blend(x + col, bottom, pixel);
            Blend(x + w - 1 - col, bottom, pixel);
        }
        FillRect(x + col, top, w - 2 * col, 1, color);
        FillRect(x + col, bottom, w - 2 * col, 1, color);
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
    u32& dst = m_pixels[y * m_width + x];
    dst = Mix(dst, src);
}

void Canvas::DrawImage(int x, int y, int w, int h, const u32* pixels)
{
    int x0 = x < m_clipX0 ? m_clipX0 : x;
    int y0 = y < m_clipY0 ? m_clipY0 : y;
    int x1 = x + w > m_clipX1 ? m_clipX1 : x + w;
    int y1 = y + h > m_clipY1 ? m_clipY1 : y + h;
    for (int row = y0; row < y1; row++)
    {
        u32* line = &m_pixels[row * m_width];
        const u32* src = pixels + (row - y) * w - x;
        for (int col = x0; col < x1; col++)
            line[col] = Mix(line[col], src[col]);
    }
}

void Canvas::DrawImageScaled(int x, int y, int w, int h, const u32* pixels, int sw, int sh)
{
    std::vector<u32> scaled;
    ScaleImage(pixels, sw, sh, w, h, &scaled);
    DrawImage(x, y, w, h, &scaled[0]);
}

void Canvas::ScaleImage(const u32* pixels, int sw, int sh, int w, int h, std::vector<u32>* out)
{
    out->resize(w * h);
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
            u32 pixel = 0;
            for (int shift = 0; shift < 32; shift += 8)
            {
                u32 top = ((p00 >> shift) & 0xFF) * (256 - fx) + ((p01 >> shift) & 0xFF) * fx;
                u32 bottom = ((p10 >> shift) & 0xFF) * (256 - fx) + ((p11 >> shift) & 0xFF) * fx;
                pixel |= ((top * (256 - fy) + bottom * fy) >> 16) << shift;
            }
            (*out)[row * w + col] = pixel;
        }
    }
}

bool Screen::Open(int* status)
{
    // xsdk's 1280x720 front buffer is only tried on 720p and 1080p outputs
    // (third_party/xsdk/docs/HARDWARE.md); interlaced ones get the kernel's mode
    // anyway, and a 1280x1024 output stayed black with it. So anything
    // else takes the kernel's mode, and the canvas is scaled into it here.
    VIDEO_MODE video;
    memset(&video, 0, sizeof video);
    VdQueryVideoMode(&video);
    bool tested = !video.is_interlaced &&
        ((video.display_width == 1280 && video.display_height == 720) ||
         (video.display_width == 1920 && video.display_height == 1080));
    uint32_t width = 0;
    uint32_t height = 0;
    NTSTATUS s = xsdk_gpu_open();
    if (NT_SUCCESS(s))
        s = xsdk_present_open(tested ? XSDK_DISPLAY_720P : XSDK_DISPLAY_KERNEL, kFrontBuffers, kSwapInterval, &width,
                              &height);
    *status = (int)s;
    if (!NT_SUCCESS(s))
        return false;
    m_canvas.m_width = kCanvasWidth;
    m_canvas.m_height = kCanvasHeight;
    m_canvas.m_pixels.assign(kCanvasWidth * kCanvasHeight, 0xFF000000);
    m_canvas.ResetClip();
    m_version = 1;
    m_bufferVersion[0] = m_bufferVersion[1] = 0;
    m_frontWidth = (int)width;
    m_frontHeight = (int)height;
    m_viewX = m_viewY = 0;
    m_viewW = m_frontWidth;
    m_viewH = m_frontHeight;
    m_frame.clear();
    m_frameVersion = 0;
    if (m_frontWidth != kCanvasWidth || m_frontHeight != kCanvasHeight)
    {
        // The front buffer is stretched over the whole output, so on the TV it
        // has the output's shape: an HD mode's own, else 4:3 or 16:9 as set.
        u32 aspectW = video.display_width;
        u32 aspectH = video.display_height;
        if (!video.is_hi_def || !aspectW || !aspectH)
        {
            aspectW = video.is_widescreen ? 16 : 4;
            aspectH = video.is_widescreen ? 9 : 3;
        }
        // 16:9 inside that: bars above and below on a narrower output, at the sides on a wider one.
        if (16 * aspectH > 9 * aspectW)
            m_viewH = (int)((u64)m_frontHeight * 9 * aspectW / (16 * aspectH));
        else
            m_viewW = (int)((u64)m_frontWidth * 16 * aspectH / (9 * aspectW));
        if (!video.is_hi_def)
        {
            m_viewW = m_viewW * kSdSafePercent / 100;
            m_viewH = m_viewH * kSdSafePercent / 100;
        }
        m_viewX = (m_frontWidth - m_viewW) / 2;
        m_viewY = (m_frontHeight - m_viewH) / 2;
        m_frame.assign(width * height, 0xFF000000);
    }
    xsdk_log_write("screen: output %ux%u, interlaced %u, widescreen %u, hi-def %u; front buffer %ux%u, canvas at %d,%d "
                   "as %dx%d",
                   (unsigned)video.display_width, (unsigned)video.display_height, (unsigned)video.is_interlaced,
                   (unsigned)video.is_widescreen, (unsigned)video.is_hi_def, (unsigned)width, (unsigned)height,
                   m_viewX, m_viewY, m_viewW, m_viewH);
    return true;
}

// The mean of two pixels, each channel rounded down.
static inline u32 Average(u32 a, u32 b)
{
    return (a & b) + (((a ^ b) & 0xFEFEFEFE) >> 1);
}

void Screen::BuildFrame()
{
    const u32* pixels = &m_canvas.m_pixels[0];
    int sw = kCanvasWidth;
    int sh = kCanvasHeight;
    // Bilinear filtering skips source pixels below half size, and thin text
    // with them, so each such direction is halved first by averaging pairs.
    int stepX = m_viewW <= sw / 2 ? 2 : 1;
    int stepY = m_viewH <= sh / 2 ? 2 : 1;
    std::vector<u32> reduced;
    if (stepX > 1 || stepY > 1)
    {
        int rw = sw / stepX;
        int rh = sh / stepY;
        reduced.resize(rw * rh);
        for (int row = 0; row < rh; row++)
        {
            const u32* top = pixels + row * stepY * sw;
            const u32* bottom = top + (stepY - 1) * sw;
            u32* out = &reduced[row * rw];
            for (int col = 0; col < rw; col++)
            {
                int x0 = col * stepX;
                int x1 = x0 + stepX - 1;
                out[col] = Average(Average(top[x0], top[x1]), Average(bottom[x0], bottom[x1]));
            }
        }
        pixels = &reduced[0];
        sw = rw;
        sh = rh;
    }
    std::vector<u32> scaled;
    Canvas::ScaleImage(pixels, sw, sh, m_viewW, m_viewH, &scaled);
    // The borders around it stay black.
    for (int row = 0; row < m_viewH; row++)
        memcpy(&m_frame[(m_viewY + row) * m_frontWidth + m_viewX], &scaled[row * m_viewW], m_viewW * sizeof(u32));
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
        if (!m_frame.empty() && m_frameVersion != m_version)
        {
            BuildFrame();
            m_frameVersion = m_version;
        }
        const u32* pixels = m_frame.empty() ? &m_canvas.m_pixels[0] : &m_frame[0];
        // The front buffer stores each pixel's bytes B, G, R, A, and is
        // write-combined memory: volatile 32-bit stores only.
        volatile uint32_t* front = xsdk_present_pixels(address);
        for (int y = 0; y < m_frontHeight; y++)
        {
            const u32* line = &pixels[y * m_frontWidth];
            // Inside a tile, runs of 4 pixels (x & 3) are next to each other,
            // so the address is worked out once per run.
            for (int x = 0; x < m_frontWidth; x += 4)
            {
                volatile uint32_t* run = front + TiledIndex(x, y, m_frontWidth);
                run[0] = __builtin_bswap32(line[x]);
                run[1] = __builtin_bswap32(line[x + 1]);
                run[2] = __builtin_bswap32(line[x + 2]);
                run[3] = __builtin_bswap32(line[x + 3]);
            }
        }
#if defined(__powerpc__)
        __asm__ volatile("sync" ::: "memory");
#endif
        m_bufferVersion[i] = m_version;
    }
    xsdk_present_end();
}
