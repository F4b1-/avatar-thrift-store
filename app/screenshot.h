// Screenshots of the app: the canvas as a PNG in game:\Screenshots
// (AvatarThriftStore_001.png, _002, ...), next to the app.
#pragma once

#include "../core/types.h"

#include <string>
#include <vector>

// Saves `pixels` (ARGB, width x height) under the next free name; `path` is the
// file written. False if encoding or writing failed.
bool SaveScreenshot(const std::vector<u32>& pixels, int width, int height, std::string* path);
