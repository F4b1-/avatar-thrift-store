// stb_image (third_party/stb_image.h, v2.30, public domain / MIT), PNG only,
// compiled on its own so its unused helpers don't warn in the app's code.
#pragma GCC diagnostic ignored "-Wunused-function"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"
