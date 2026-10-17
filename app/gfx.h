// A CPU-drawn canvas shown through xsdk's front buffers, as in the hello_world
// port: the canvas is ARGB in ordinary memory, and each front buffer gets a
// tiled copy whenever it holds an older picture.
#pragma once

#include "../core/types.h"

#include <string>
#include <vector>

class Canvas
{
public:
    int Width() const { return m_width; }
    int Height() const { return m_height; }

    void Clear(u32 color);
    // Colors are ARGB; anything below full alpha is blended over the canvas.
    void FillRect(int x, int y, int w, int h, u32 color);
    void FrameRect(int x, int y, int w, int h, int thickness, u32 color);
    // Corners of `radius`, with smoothed edges.
    void FillRoundRect(int x, int y, int w, int h, int radius, u32 color);
    void FillCircle(int centerX, int centerY, int radius, u32 color);
    // From `top` at the first row to `bottom` at the last (opaque colors).
    void FillGradient(int x, int y, int w, int h, u32 top, u32 bottom);
    // Copies the whole picture out and back, to reuse a background.
    void SaveTo(std::vector<u32>* pixels) const { *pixels = m_pixels; }
    void LoadFrom(const std::vector<u32>& pixels)
    {
        if (pixels.size() == m_pixels.size())
            m_pixels = pixels;
    }
    // Text in the 16x32 font, cut off at `maxWidth` pixels (0: no limit) with "...".
    void DrawText(int x, int y, const std::string& text, u32 color, int scale = 1, int maxWidth = 0);
    void DrawTextCentered(int y, const std::string& text, u32 color, int scale = 1);
    // An ARGB image, alpha-blended over the canvas.
    void DrawImage(int x, int y, int w, int h, const u32* pixels);
    // The same, scaled from sw x sh to w x h with bilinear filtering.
    void DrawImageScaled(int x, int y, int w, int h, const u32* pixels, int sw, int sh);
    // Only the scaling, into `out` (w x h): for images drawn often at one size.
    static void ScaleImage(const u32* pixels, int sw, int sh, int w, int h, std::vector<u32>* out);

    // Limits all drawing to a rectangle until ResetClip.
    void SetClip(int x, int y, int w, int h);
    void ResetClip();

    static int TextWidth(const std::string& text, int scale = 1);

private:
    friend class Screen;
    void Blend(int x, int y, u32 src);

    std::vector<u32> m_pixels;
    int m_width;
    int m_height;
    int m_clipX0, m_clipY0, m_clipX1, m_clipY1;
};

class Screen
{
public:
    // Opens the GPU and the display; false (and the status) if they won't open.
    bool Open(int* status);
    Canvas& canvas() { return m_canvas; }
    // Marks the canvas as changed.
    void Invalidate() { m_version++; }
    // Waits for the next front buffer, copies the canvas into it if needed and queues it.
    void Present();

private:
    Canvas m_canvas;
    u32 m_version;
    u32 m_bufferVersion[2];
};
