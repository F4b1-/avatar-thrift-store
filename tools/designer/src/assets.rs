// The app's own font and menu pictures, read from the generated headers in
// app/ (font.h, menu_art.h), so the preview uses exactly what the xex embeds.

use std::path::Path;

// The hex numbers in `text`, in order.
fn hex_numbers(text: &str) -> impl Iterator<Item = u32> + '_ {
    text.split(|c: char| !c.is_ascii_alphanumeric())
        .filter_map(|t| t.strip_prefix("0x"))
        .filter_map(|h| u32::from_str_radix(h, 16).ok())
}

pub fn load_font(app: &Path) -> Result<Vec<[u16; 32]>, String> {
    let path = app.join("font.h");
    let text = std::fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    let start = text.find("g_font").ok_or("font.h: no g_font")?;
    let body = &text[start..];
    let body = &body[body.find('{').ok_or("font.h: no glyphs")?..];
    let rows: Vec<u16> = hex_numbers(body).map(|v| v as u16).collect();
    let glyphs: Vec<[u16; 32]> = rows.as_chunks::<32>().0.to_vec();
    if glyphs.len() != 95 {
        return Err(format!("font.h: {} glyphs, expected 95", glyphs.len()));
    }
    Ok(glyphs)
}

pub struct Picture {
    pub width: i32,
    pub height: i32,
    pub argb: Vec<u32>,
}

// The menu cards' pictures; an empty list if menu_art.h can't be read (the
// preview then shows the kHero colours, as the app does).
pub fn load_menu_art(app: &Path) -> Vec<Picture> {
    let Ok(text) = std::fs::read_to_string(app.join("menu_art.h")) else {
        return Vec::new();
    };
    let mut pictures = Vec::new();
    for i in 0..3 {
        let marker = format!("g_menuArt{i}[");
        let Some(start) = text.find(&marker) else {
            return Vec::new();
        };
        let body = &text[start..];
        let (Some(open), Some(close)) = (body.find('{'), body.find("};")) else {
            return Vec::new();
        };
        let bytes: Vec<u8> = hex_numbers(&body[open..close]).map(|v| v as u8).collect();
        match decode_png(&bytes) {
            Some(p) => pictures.push(p),
            None => return Vec::new(),
        }
    }
    pictures
}

pub fn decode_png(bytes: &[u8]) -> Option<Picture> {
    let mut decoder = png::Decoder::new(std::io::Cursor::new(bytes));
    decoder.set_transformations(
        png::Transformations::normalize_to_color8() | png::Transformations::ALPHA,
    );
    let mut reader = decoder.read_info().ok()?;
    let mut buf = vec![0; reader.output_buffer_size()?];
    let info = reader.next_frame(&mut buf).ok()?;
    if info.color_type != png::ColorType::Rgba {
        return None;
    }
    let argb = buf[..info.buffer_size()]
        .as_chunks::<4>()
        .0
        .iter()
        .map(|p| (p[3] as u32) << 24 | (p[0] as u32) << 16 | (p[1] as u32) << 8 | p[2] as u32)
        .collect();
    Some(Picture {
        width: info.width as i32,
        height: info.height as i32,
        argb,
    })
}

pub fn write_png(path: &Path, width: u32, height: u32, argb: &[u32]) -> Result<(), String> {
    let file = std::fs::File::create(path).map_err(|e| e.to_string())?;
    let mut encoder = png::Encoder::new(std::io::BufWriter::new(file), width, height);
    encoder.set_color(png::ColorType::Rgb);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header().map_err(|e| e.to_string())?;
    let rgb: Vec<u8> = argb
        .iter()
        .flat_map(|p| [(p >> 16) as u8, (p >> 8) as u8, *p as u8])
        .collect();
    writer.write_image_data(&rgb).map_err(|e| e.to_string())
}
