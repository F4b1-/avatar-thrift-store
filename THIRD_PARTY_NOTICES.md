# Third-party notices

Avatar Thrift Store's own code is under the MIT License (`LICENSE`). It
includes or is built with the following.

## stb_image and stb_image_write (third_party/)

stb_image v2.30 and stb_image_write v1.16 by Sean Barrett,
https://github.com/nothings/stb, used for decoding item previews and writing
screenshots (PNG). Available under the MIT License or as public domain
(Unlicense), at your choice; the full text is at the end of each file.

## tiled_index (app/gfx.cpp)

The front buffer's tile addressing comes from libxenon
(drivers/console/console.c), as carried by xsdk's tests/tiled_index.h:

```
Copyright 2009  Georg Lukas <georg@op-co.de>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
```

## Cascadia Mono (app/font.h)

The app's bitmap font is rendered from Cascadia Mono by `tools/gen_font.ps1`.
Cascadia Mono is Copyright (c) Microsoft Corporation, licensed under the SIL
Open Font License, Version 1.1 (https://openfontlicense.org,
https://github.com/microsoft/cascadia-code).

## xsdk

The console app is built with and statically linked against xsdk,
https://github.com/xenosworks/xsdk, Copyright (c) 2026 Mena, MIT License. See
its LICENSE and THIRD_PARTY_NOTICES.md for what it includes.
