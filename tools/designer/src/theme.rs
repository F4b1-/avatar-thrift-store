// The values in app/theme.h: read from it, written back in the same layout.

use std::collections::HashMap;

#[derive(Clone, PartialEq)]
pub struct Theme {
    pub sky_top: u32,
    pub sky_bottom: u32,
    pub glow: u32,
    pub card: u32,
    pub card_picked: u32,
    pub green: u32,
    pub white: u32,
    pub text: u32,
    pub dim: u32,
    pub good: u32,
    pub warn: u32,
    pub bad: u32,
    pub key: u32,
    pub line: u32,
    pub shade: u32,
    pub button_b: u32,
    pub button_x: u32,
    pub button_y: u32,
    pub button_pill: u32,
    pub pastels: [u32; 5],
    pub brights: [u32; 5],
    pub hero: [u32; 3],
    pub margin_x: i32,
    pub list_top: i32,
    pub row_height: i32,
    pub view_bottom: i32,
    pub info_y: i32,
    pub footer_y: i32,
    pub tile_columns: i32,
    pub tile_width: i32,
    pub tile_height: i32,
    pub tile_image: i32,
}

impl Default for Theme {
    // The app's design as shipped.
    fn default() -> Self {
        Theme {
            sky_top: 0xFF223029,
            sky_bottom: 0xFF141B18,
            glow: 0x02F3D9A8,
            card: 0xFF2A3631,
            card_picked: 0xFF33413A,
            green: 0xFF6FA35A,
            white: 0xFFFAF5EC,
            text: 0xFFECE5D8,
            dim: 0xFFBCCDC8,
            good: 0xFF8CC475,
            warn: 0xFFE3AE4F,
            bad: 0xFFE07A5F,
            key: 0xFF36443E,
            line: 0x1AF3ECE0,
            shade: 0x80000000,
            button_b: 0xFFC8604F,
            button_x: 0xFF5B8DB8,
            button_y: 0xFFD9A441,
            button_pill: 0xFF4A5751,
            pastels: [0xFF3D5245, 0xFF3A4A55, 0xFF4D4238, 0xFF453F52, 0xFF4C4A36],
            brights: [0xFF6FA35A, 0xFF5B8DB8, 0xFFC9824F, 0xFF8C76B0, 0xFFC9A44A],
            hero: [0xFF5E8F4C, 0xFF6E5C8A, 0xFF4F6F8F],
            margin_x: 80,
            list_top: 152,
            row_height: 78,
            view_bottom: 592,
            info_y: 604,
            footer_y: 652,
            tile_columns: 5,
            tile_width: 224,
            tile_height: 212,
            tile_image: 152,
        }
    }
}

// A colour's name in theme.h and what it is for.
pub struct ColorField<'a> {
    pub name: &'static str,
    pub help: &'static str,
    pub value: &'a mut u32,
}

pub struct ColorList<'a> {
    pub name: &'static str,
    pub help: &'static str,
    pub values: &'a mut [u32],
}

pub struct IntField<'a> {
    pub name: &'static str,
    pub help: &'static str,
    pub range: (i32, i32),
    pub value: &'a mut i32,
}

impl Theme {
    pub fn colors(&mut self) -> Vec<ColorField<'_>> {
        macro_rules! c {
            ($name:literal, $help:literal, $field:ident) => {
                ColorField {
                    name: $name,
                    help: $help,
                    value: &mut self.$field,
                }
            };
        }
        vec![
            c!("kColorSkyTop", "background, top", sky_top),
            c!("kColorSkyBottom", "background, bottom", sky_bottom),
            c!(
                "kColorGlow",
                "faint glow behind the title, drawn many times over",
                glow
            ),
            c!("kColorCard", "rows, dialogs, menu cards", card),
            c!("kColorCardPicked", "a selected row or tile", card_picked),
            c!(
                "kColorGreen",
                "selection rim, A button, dialog title band",
                green
            ),
            c!("kColorWhite", "text on coloured surfaces", white),
            c!("kColorText", "text on dark surfaces", text),
            c!("kColorDim", "secondary text", dim),
            c!("kColorGood", "\"ready\" states", good),
            c!("kColorWarn", "warnings", warn),
            c!("kColorBad", "errors", bad),
            c!("kColorKey", "keys, dialog options, placeholders", key),
            c!(
                "kColorLine",
                "the line under the header, scroll bar track",
                line
            ),
            c!("kColorShade", "behind dialogs", shade),
            c!("kColorButtonB", "", button_b),
            c!("kColorButtonX", "", button_x),
            c!("kColorButtonY", "", button_y),
            c!(
                "kColorButtonPill",
                "BACK, LB/RB and other wide buttons",
                button_pill
            ),
        ]
    }

    pub fn color_lists(&mut self) -> Vec<ColorList<'_>> {
        vec![
            ColorList {
                name: "kPastels",
                help: "spotlights behind item previews",
                values: &mut self.pastels,
            },
            ColorList {
                name: "kBrights",
                help: "folder and gamerpic colours, picked by name",
                values: &mut self.brights,
            },
            ColorList {
                name: "kHero",
                help: "menu cards, if their pictures can't be decoded",
                values: &mut self.hero,
            },
        ]
    }

    pub fn layout(&mut self) -> Vec<IntField<'_>> {
        macro_rules! i {
            ($name:literal, $help:literal, $lo:literal, $hi:literal, $field:ident) => {
                IntField {
                    name: $name,
                    help: $help,
                    range: ($lo, $hi),
                    value: &mut self.$field,
                }
            };
        }
        vec![
            i!("kMarginX", "left and right margin", 0, 320, margin_x),
            i!(
                "kListTop",
                "where lists and the grid start",
                100,
                400,
                list_top
            ),
            i!("kRowHeight", "folder and profile rows", 40, 200, row_height),
            i!(
                "kViewBottom",
                "lists and the item grid scroll above this",
                200,
                700,
                view_bottom
            ),
            i!(
                "kInfoY",
                "the selected item's name and state",
                200,
                700,
                info_y
            ),
            i!("kFooterY", "button hints", 200, 700, footer_y),
            i!("kTileColumns", "tiles across", 1, 10, tile_columns),
            i!("kTileWidth", "", 60, 1280, tile_width),
            i!("kTileHeight", "", 60, 600, tile_height),
            i!(
                "kTileImage",
                "the preview's size in a tile",
                32,
                512,
                tile_image
            ),
        ]
    }

    // Reads theme.h. Values it doesn't have keep their defaults; the second
    // part lists what was missing or couldn't be read.
    pub fn parse(source: &str) -> (Theme, Vec<String>) {
        let mut found: HashMap<String, Vec<i64>> = HashMap::new();
        let mut notes = Vec::new();
        for line in source.lines() {
            let line = line.trim();
            let Some(rest) = line.strip_prefix("static const ") else {
                continue;
            };
            let Some((left, right)) = rest.split_once('=') else {
                continue;
            };
            let name = left.split_whitespace().last().unwrap_or("");
            let name = name.split('[').next().unwrap_or("").to_string();
            let value = right
                .split(';')
                .next()
                .unwrap_or("")
                .trim()
                .trim_start_matches('{')
                .trim_end_matches('}');
            let numbers: Result<Vec<i64>, _> =
                value.split(',').map(|v| parse_number(v.trim())).collect();
            match numbers {
                Ok(n) => {
                    found.insert(name, n);
                }
                Err(_) => notes.push(format!("{name}: can't read \"{}\"", value.trim())),
            }
        }
        let mut theme = Theme::default();
        let mut take = |name: &str, slots: &mut [&mut i64]| match found.get(name) {
            Some(v) if v.len() == slots.len() => {
                for (s, n) in slots.iter_mut().zip(v) {
                    **s = *n;
                }
            }
            Some(v) => notes.push(format!(
                "{name}: {} values, expected {}",
                v.len(),
                slots.len()
            )),
            None => notes.push(format!("{name}: missing, using the default")),
        };
        for f in theme.colors() {
            let mut v = *f.value as i64;
            take(f.name, &mut [&mut v]);
            *f.value = v as u32;
        }
        for f in theme.color_lists() {
            let mut v: Vec<i64> = f.values.iter().map(|&c| c as i64).collect();
            take(f.name, &mut v.iter_mut().collect::<Vec<_>>());
            for (dst, src) in f.values.iter_mut().zip(v) {
                *dst = src as u32;
            }
        }
        for f in theme.layout() {
            let mut v = *f.value as i64;
            take(f.name, &mut [&mut v]);
            *f.value = v as i32;
        }
        (theme, notes)
    }

    // theme.h for these values.
    pub fn to_header(&self) -> String {
        let mut t = self.clone();
        let mut out = String::new();
        out += "// The app's colours and layout. Written by tools/designer (which reads it back\n";
        out += "// in); hand edits are fine as long as each value keeps its own line.\n";
        out += "#pragma once\n\n#include \"../core/types.h\"\n\n";
        out += "// A cosy store at night: dark forest-green surfaces with cream text, muted\n";
        out += "// spotlights behind the items, sage and amber accents. Colours are ARGB.\n";
        for f in t.colors() {
            let decl = format!("static const u32 {:<17} = 0x{:08X};", f.name, *f.value);
            out += &if f.help.is_empty() {
                decl
            } else {
                format!("{decl}   // {}", f.help)
            };
            out += "\n";
        }
        let lists: Vec<String> = t
            .color_lists()
            .iter()
            .map(|f| {
                let values: Vec<String> = f.values.iter().map(|v| format!("0x{v:08X}")).collect();
                format!(
                    "static const u32 {}[{}] = {{ {} }};\n",
                    f.name,
                    f.values.len(),
                    values.join(", ")
                )
            })
            .collect();
        out += "\n// Spotlights behind item previews, and icon colors picked by name.\n";
        out += &lists[0];
        out += &lists[1];
        out += "// The main menu cards' colours, shown if their pictures can't be decoded.\n";
        out += &lists[2];
        let l = &self;
        out += "\n// Inside the title-safe area of a TV.\n";
        out += &format!("static const int kMarginX = {};\n", l.margin_x);
        out += &format!("static const int kListTop = {};\n", l.list_top);
        out += &format!("static const int kRowHeight = {};\n", l.row_height);
        out += &format!(
            "{:<40} // lists and the item grid scroll above this\n",
            format!("static const int kViewBottom = {};", l.view_bottom)
        );
        out += &format!(
            "{:<40} // the selected item's name and state\n",
            format!("static const int kInfoY = {};", l.info_y)
        );
        out += &format!("static const int kFooterY = {};\n", l.footer_y);
        out += &format!(
            "\n// Item tiles: {} across, the 128 x 128 preview shown at {} x {}.\n",
            l.tile_columns, l.tile_image, l.tile_image
        );
        out += &format!("static const int kTileColumns = {};\n", l.tile_columns);
        out += &format!("static const int kTileWidth = {};\n", l.tile_width);
        out += &format!("static const int kTileHeight = {};\n", l.tile_height);
        out += &format!("static const int kTileImage = {};\n", l.tile_image);
        out
    }

    // Layouts the app can't draw (errors, saving is refused) and things that
    // will look wrong or cost speed (warnings).
    pub fn check(&self) -> (Vec<String>, Vec<String>) {
        let mut errors = Vec::new();
        let mut warnings = Vec::new();
        let t = self;
        if t.view_bottom - t.list_top < t.row_height {
            errors.push("kViewBottom - kListTop must fit one row (kRowHeight)".into());
        }
        if t.view_bottom - t.list_top < t.tile_height {
            errors.push("kViewBottom - kListTop must fit one row of tiles (kTileHeight)".into());
        }
        if t.tile_columns * t.tile_width > 1280 {
            errors.push(format!(
                "{} tiles of {} px are wider than the screen",
                t.tile_columns, t.tile_width
            ));
        }
        if t.tile_image > t.tile_width - 20 {
            errors.push("kTileImage must be at most kTileWidth - 20 (the card's width)".into());
        }
        if 684 - (t.view_bottom + 4) < 40 {
            errors.push("kViewBottom leaves the toast under the list less than 40 px".into());
        }
        if t.margin_x * 2 + 200 > 1280 {
            errors.push("kMarginX leaves no room for the screens".into());
        }
        if t.tile_image + 6 + 34 > t.tile_height - 12 {
            warnings.push("the item name doesn't fit under the preview in a tile".into());
        }
        let grid_left = (1280 - t.tile_columns * t.tile_width) / 2 + 10;
        if grid_left < 64 {
            warnings.push(
                "the tile grid reaches outside the title-safe area (64 px from the edge)".into(),
            );
        }
        if t.margin_x < 64 {
            warnings.push("kMarginX under 64 px is outside the title-safe area on some TVs".into());
        }
        if t.list_top < 140 {
            warnings.push("kListTop under 140 runs into the header line at y 128".into());
        }
        if t.row_height < 72 {
            warnings.push("rows under 72 px cut off the folder icons and gamerpics".into());
        }
        if t.info_y < t.view_bottom {
            warnings.push("kInfoY is above kViewBottom: the info line covers the list".into());
        }
        if t.footer_y < t.info_y + 40 {
            warnings.push("kFooterY is less than 40 px below kInfoY: the lines overlap".into());
        }
        // The shipped footer ends at y 688, a little past the title-safe 684.
        if t.footer_y + 36 > 696 {
            warnings.push("the footer is close to the bottom edge, which some TVs cut off".into());
        }
        // Text on the dark surfaces, and the bold labels on coloured pills
        // (which read at less contrast: the shipped ones are about 2.7:1).
        let pairs = [
            ("kColorText on kColorCard", t.text, t.card, 4.5),
            ("kColorDim on kColorCard", t.dim, t.card, 3.0),
            ("kColorText on kColorSkyBottom", t.text, t.sky_bottom, 4.5),
            ("kColorText on kColorKey", t.text, t.key, 4.5),
            ("kColorWhite on kColorGreen", t.white, t.green, 2.2),
            ("kColorWhite on kColorBad", t.white, t.bad, 2.2),
        ];
        for (what, fg, bg, least) in pairs {
            let ratio = contrast(fg, bg);
            if ratio < least {
                warnings.push(format!(
                    "{what}: contrast {ratio:.1}:1 is hard to read on a TV"
                ));
            }
        }
        if t.shade != 0x80000000 {
            warnings.push(
                "kColorShade other than 0x80000000 is drawn without the console's fast path".into(),
            );
        }
        if t.sky_top >> 24 != 0xFF || t.sky_bottom >> 24 != 0xFF {
            warnings.push("the background gradient ignores alpha".into());
        }
        (errors, warnings)
    }
}

fn parse_number(s: &str) -> Result<i64, std::num::ParseIntError> {
    let s = s.trim_end_matches(['u', 'U', 'l', 'L']);
    match s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        Some(hex) => i64::from_str_radix(hex, 16),
        None => s.parse(),
    }
}

// WCAG contrast ratio of two opaque colours.
pub fn contrast(a: u32, b: u32) -> f32 {
    fn luminance(c: u32) -> f32 {
        let ch = |shift: u32| {
            let v = ((c >> shift) & 0xFF) as f32 / 255.0;
            if v <= 0.03928 {
                v / 12.92
            } else {
                ((v + 0.055) / 1.055).powf(2.4)
            }
        };
        0.2126 * ch(16) + 0.7152 * ch(8) + 0.0722 * ch(0)
    }
    let (la, lb) = (luminance(a), luminance(b));
    (la.max(lb) + 0.05) / (la.min(lb) + 0.05)
}

#[cfg(test)]
mod tests {
    use super::*;

    const SHIPPED: &str = include_str!("../../../app/theme.h");

    #[test]
    fn writes_the_shipped_header_from_the_defaults() {
        assert_eq!(Theme::default().to_header(), SHIPPED.replace("\r\n", "\n"));
    }

    #[test]
    fn reads_the_shipped_header() {
        let (theme, notes) = Theme::parse(SHIPPED);
        assert!(notes.is_empty(), "{notes:?}");
        assert!(theme == Theme::default());
    }

    #[test]
    fn round_trips_changes() {
        let mut theme = Theme::default();
        theme.card = 0xFF112233;
        theme.brights[4] = 0x80FFFFFF;
        theme.tile_columns = 4;
        let (back, notes) = Theme::parse(&theme.to_header());
        assert!(notes.is_empty(), "{notes:?}");
        assert!(back == theme);
    }

    #[test]
    fn keeps_defaults_for_missing_values() {
        let (theme, notes) = Theme::parse("static const u32 kColorCard = 0xFF000000;\n");
        assert_eq!(theme.card, 0xFF000000);
        assert_eq!(theme.green, Theme::default().green);
        assert!(notes.iter().any(|n| n.starts_with("kColorGreen")));
    }

    #[test]
    fn the_shipped_layout_passes_the_checks() {
        let (errors, warnings) = Theme::default().check();
        assert!(errors.is_empty(), "{errors:?}");
        assert!(warnings.is_empty(), "{warnings:?}");
    }

    #[test]
    fn refuses_a_grid_wider_than_the_screen() {
        let theme = Theme {
            tile_columns: 6,
            ..Theme::default()
        };
        assert!(!theme.check().0.is_empty());
    }
}
