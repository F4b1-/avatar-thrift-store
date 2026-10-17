// stb_image (third_party/stb_image.h, v2.30) for reading the items' PNG
// previews and stb_image_write (v1.16) for writing screenshots, both public
// domain / MIT, compiled on their own so their unused helpers don't warn in the
// app's code. No stdio: files go through the app's storage code.
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
