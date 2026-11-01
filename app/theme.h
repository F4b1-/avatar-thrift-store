// The app's colours and layout. Written by tools/designer (which reads it back
// in); hand edits are fine as long as each value keeps its own line.
#pragma once

#include "../core/types.h"

// A cosy store at night: dark forest-green surfaces with cream text, muted
// spotlights behind the items, sage and amber accents. Colours are ARGB.
static const u32 kColorSkyTop      = 0xFF345E17;   // background, top
static const u32 kColorSkyBottom   = 0xFF1B3608;   // background, bottom
static const u32 kColorGlow        = 0x02F3D9A8;   // faint glow behind the title, drawn many times over
static const u32 kColorCard        = 0xFF2A3631;   // rows, dialogs, menu cards
static const u32 kColorCardPicked  = 0xFF33413A;   // a selected row or tile
static const u32 kColorGreen       = 0xFF6FA35A;   // selection rim, A button, dialog title band
static const u32 kColorWhite       = 0xFFFAF5EC;   // text on coloured surfaces
static const u32 kColorText        = 0xFFECE5D8;   // text on dark surfaces
static const u32 kColorDim         = 0xFFBCCDC8;   // secondary text
static const u32 kColorGood        = 0xFF8CC475;   // "ready" states
static const u32 kColorWarn        = 0xFFE3AE4F;   // warnings
static const u32 kColorBad         = 0xFFE07A5F;   // errors
static const u32 kColorKey         = 0xFF36443E;   // keys, dialog options, placeholders
static const u32 kColorLine        = 0x1AF3ECE0;   // the line under the header, scroll bar track
static const u32 kColorShade       = 0x80000000;   // behind dialogs
static const u32 kColorButtonB     = 0xFFC8604F;
static const u32 kColorButtonX     = 0xFF5B8DB8;
static const u32 kColorButtonY     = 0xFFD9A441;
static const u32 kColorButtonPill  = 0xFF4A5751;   // BACK, LB/RB and other wide buttons

// Spotlights behind item previews, and icon colors picked by name.
static const u32 kPastels[5] = { 0xFF3D5245, 0xFF3A4A55, 0xFF4D4238, 0xFF453F52, 0xFF4C4A36 };
static const u32 kBrights[5] = { 0xFF6FA35A, 0xFF5B8DB8, 0xFFC9824F, 0xFF8C76B0, 0xFFC9A44A };
// The main menu cards' colours, shown if their pictures can't be decoded.
static const u32 kHero[3] = { 0xFF5E8F4C, 0xFF6E5C8A, 0xFF4F6F8F };

// Inside the title-safe area of a TV.
static const int kMarginX = 80;
static const int kListTop = 152;
static const int kRowHeight = 78;
static const int kViewBottom = 592;      // lists and the item grid scroll above this
static const int kInfoY = 604;           // the selected item's name and state
static const int kFooterY = 652;

// Item tiles: 5 across, the 128 x 128 preview shown at 152 x 152.
static const int kTileColumns = 5;
static const int kTileWidth = 224;
static const int kTileHeight = 212;
static const int kTileImage = 152;
