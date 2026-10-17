#include "screenshot.h"

#include "storage.h"

#include "stb_image_write.h"

#include <stdio.h>

static const char* kScreenshotFolder = "Screenshots";

static void AppendBytes(void* context, void* data, int size)
{
    std::vector<u8>* out = (std::vector<u8>*)context;
    out->insert(out->end(), (const u8*)data, (const u8*)data + size);
}

bool SaveScreenshot(const std::vector<u32>& pixels, int width, int height, std::string* path)
{
    if (pixels.size() != (size_t)width * height)
        return false;
    std::vector<u8> rgb((size_t)width * height * 3);
    for (size_t i = 0; i < pixels.size(); i++)
    {
        rgb[i * 3] = (u8)(pixels[i] >> 16);
        rgb[i * 3 + 1] = (u8)(pixels[i] >> 8);
        rgb[i * 3 + 2] = (u8)pixels[i];
    }
    std::vector<u8> png;
    if (!stbi_write_png_to_func(AppendBytes, &png, width, height, 3, &rgb[0], width * 3) || png.empty())
        return false;

    if (!MakeDirs("game:", kScreenshotFolder))
        return false;
    for (int n = 1; n < 1000; n++)
    {
        char name[96];
        snprintf(name, sizeof name, "game:\\%s\\AvatarThriftStore_%03d.png", kScreenshotFolder, n);
        if (PathExists(name))
            continue;
        *path = name;
        return WriteWholeFile(name, &png[0], (u32)png.size());
    }
    return false;
}
