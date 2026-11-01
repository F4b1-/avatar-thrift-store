// The app's CPU canvas (app/gfx.cpp's Canvas), ported line by line so the
// preview draws the same pixels as the console. ARGB, 1280 x 720.

pub const WIDTH: i32 = 1280;
pub const HEIGHT: i32 = 720;

pub const FONT_CELL_W: i32 = 16;
pub const FONT_CELL_H: i32 = 32;
pub const FONT_FIRST: u8 = 32;
pub const FONT_LAST: u8 = 126;

pub struct Canvas<'f> {
    pub pixels: Vec<u32>,
    pub width: i32,
    pub height: i32,
    font: &'f [[u16; 32]],
    clip: (i32, i32, i32, i32),
}

// `src` over `dst` by src's alpha, rounded as the console does.
fn mix(dst: u32, src: u32) -> u32 {
    let a = src >> 24;
    if a == 0xFF {
        return src;
    }
    if a == 0 {
        return dst;
    }
    let ia = 255 - a;
    let mut r = ((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * ia;
    let mut g = ((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * ia;
    let mut b = (src & 0xFF) * a + (dst & 0xFF) * ia;
    r = (r + 1 + (r >> 8)) >> 8;
    g = (g + 1 + (g >> 8)) >> 8;
    b = (b + 1 + (b >> 8)) >> 8;
    0xFF00_0000 | r << 16 | g << 8 | b
}

impl<'f> Canvas<'f> {
    pub fn new(width: i32, height: i32, fill: u32, font: &'f [[u16; 32]]) -> Self {
        Canvas {
            pixels: vec![fill; (width * height) as usize],
            width,
            height,
            font,
            clip: (0, 0, width, height),
        }
    }

    pub fn set_clip(&mut self, x: i32, y: i32, w: i32, h: i32) {
        self.clip = (
            x.max(0),
            y.max(0),
            (x + w).min(self.width),
            (y + h).min(self.height),
        );
    }

    pub fn reset_clip(&mut self) {
        self.set_clip(0, 0, self.width, self.height);
    }

    pub fn fill_rect(&mut self, x: i32, y: i32, w: i32, h: i32, color: u32) {
        let (cx0, cy0, cx1, cy1) = self.clip;
        let (x0, y0, x1, y1) = (x.max(cx0), y.max(cy0), (x + w).min(cx1), (y + h).min(cy1));
        if color >> 24 == 0 {
            return;
        }
        for row in y0..y1 {
            let line = (row * self.width) as usize;
            for col in x0..x1 {
                let p = &mut self.pixels[line + col as usize];
                *p = if color == 0x8000_0000 {
                    0xFF00_0000 | ((*p >> 1) & 0x7F7F7F)
                } else {
                    mix(*p, color)
                };
            }
        }
    }

    fn blend(&mut self, x: i32, y: i32, src: u32) {
        let (cx0, cy0, cx1, cy1) = self.clip;
        if x < cx0 || x >= cx1 || y < cy0 || y >= cy1 {
            return;
        }
        let p = &mut self.pixels[(y * self.width + x) as usize];
        *p = mix(*p, src);
    }

    pub fn fill_round_rect(&mut self, x: i32, y: i32, w: i32, h: i32, radius: i32, color: u32) {
        if w <= 0 || h <= 0 {
            return;
        }
        let mut r = radius;
        if 2 * r > w {
            r = w / 2;
        }
        if 2 * r > h {
            r = h / 2;
        }
        self.fill_rect(x, y + r, w, h - 2 * r, color);
        let alpha = color >> 24;
        for row in 0..r {
            let top = y + row;
            let bottom = y + h - 1 - row;
            let dy = r as f32 - row as f32 - 0.5;
            let mut col = 0;
            while col < r {
                let dx = r as f32 - col as f32 - 0.5;
                let cover = r as f32 + 0.5 - (dx * dx + dy * dy).sqrt();
                if cover >= 1.0 {
                    break;
                }
                if cover > 0.0 {
                    let pixel = ((alpha as f32 * cover) as u32) << 24 | (color & 0xFFFFFF);
                    self.blend(x + col, top, pixel);
                    self.blend(x + w - 1 - col, top, pixel);
                    self.blend(x + col, bottom, pixel);
                    self.blend(x + w - 1 - col, bottom, pixel);
                }
                col += 1;
            }
            self.fill_rect(x + col, top, w - 2 * col, 1, color);
            self.fill_rect(x + col, bottom, w - 2 * col, 1, color);
        }
    }

    pub fn fill_circle(&mut self, cx: i32, cy: i32, radius: i32, color: u32) {
        self.fill_round_rect(
            cx - radius,
            cy - radius,
            2 * radius,
            2 * radius,
            radius,
            color,
        );
    }

    pub fn fill_gradient(&mut self, x: i32, y: i32, w: i32, h: i32, top: u32, bottom: u32) {
        for row in 0..h {
            let mut color = 0xFF00_0000u32;
            for shift in [0, 8, 16] {
                let a = ((top >> shift) & 0xFF) as i32;
                let b = ((bottom >> shift) & 0xFF) as i32;
                color |= ((a + (b - a) * row / if h > 1 { h - 1 } else { 1 }) as u32) << shift;
            }
            self.fill_rect(x, y + row, w, 1, color);
        }
    }

    pub fn text_width(text: &str, scale: i32) -> i32 {
        text.len() as i32 * FONT_CELL_W * scale
    }

    // Text in the 16x32 font, cut off at `max_width` pixels (0: no limit) with "...".
    pub fn draw_text(
        &mut self,
        mut x: i32,
        y: i32,
        text: &str,
        color: u32,
        scale: i32,
        max_width: i32,
    ) {
        let mut shown: Vec<u8> = text.bytes().collect();
        if max_width > 0 && Self::text_width(text, scale) > max_width {
            let fit = (max_width / (FONT_CELL_W * scale)).max(0) as usize;
            shown.truncate(if fit > 3 { fit - 3 } else { fit });
            if fit > 3 {
                shown.extend_from_slice(b"...");
            }
        }
        for ch in shown {
            let ch = if (FONT_FIRST..=FONT_LAST).contains(&ch) {
                ch
            } else {
                b'?'
            };
            let glyph = self.font[(ch - FONT_FIRST) as usize];
            for gy in 0..FONT_CELL_H {
                let bits = glyph[gy as usize];
                for gx in 0..FONT_CELL_W {
                    if bits & (0x8000 >> gx) != 0 {
                        self.fill_rect(x + gx * scale, y + gy * scale, scale, scale, color);
                    }
                }
            }
            x += FONT_CELL_W * scale;
        }
    }

    pub fn draw_image(&mut self, x: i32, y: i32, w: i32, h: i32, src: &[u32]) {
        let (cx0, cy0, cx1, cy1) = self.clip;
        for row in y.max(cy0)..(y + h).min(cy1) {
            for col in x.max(cx0)..(x + w).min(cx1) {
                let p = &mut self.pixels[(row * self.width + col) as usize];
                *p = mix(*p, src[((row - y) * w + col - x) as usize]);
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub fn draw_image_scaled(
        &mut self,
        x: i32,
        y: i32,
        w: i32,
        h: i32,
        src: &[u32],
        sw: i32,
        sh: i32,
    ) {
        let scaled = scale_image(src, sw, sh, w, h);
        self.draw_image(x, y, w, h, &scaled);
    }
}

// Bilinear scaling from sw x sh to w x h, as Canvas::ScaleImage.
pub fn scale_image(pixels: &[u32], sw: i32, sh: i32, w: i32, h: i32) -> Vec<u32> {
    let mut out = vec![0u32; (w.max(0) * h.max(0)) as usize];
    for row in 0..h {
        let sy = (((row * 2 + 1) * sh * 128) / h - 128).max(0);
        let y0 = sy >> 8;
        let y1 = if y0 + 1 < sh { y0 + 1 } else { y0 };
        let fy = (sy & 0xFF) as u32;
        for col in 0..w {
            let sx = (((col * 2 + 1) * sw * 128) / w - 128).max(0);
            let x0 = sx >> 8;
            let x1 = if x0 + 1 < sw { x0 + 1 } else { x0 };
            let fx = (sx & 0xFF) as u32;
            let at = |x: i32, y: i32| pixels[(y * sw + x) as usize];
            let (p00, p01, p10, p11) = (at(x0, y0), at(x1, y0), at(x0, y1), at(x1, y1));
            let mut pixel = 0u32;
            for shift in [0, 8, 16, 24] {
                let top = ((p00 >> shift) & 0xFF) * (256 - fx) + ((p01 >> shift) & 0xFF) * fx;
                let bottom = ((p10 >> shift) & 0xFF) * (256 - fx) + ((p11 >> shift) & 0xFF) * fx;
                pixel |= ((top * (256 - fy) + bottom * fy) >> 16) << shift;
            }
            out[(row * w + col) as usize] = pixel;
        }
    }
    out
}
