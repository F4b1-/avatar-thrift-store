// The app's screens with made-up content, drawn as app/main.cpp draws them.
// Keep these in step with main.cpp when its drawing changes.

use crate::assets::Picture;
use crate::canvas::{Canvas, HEIGHT, WIDTH, scale_image};
use crate::theme::Theme;

const ICON_SIZE: i32 = 128;
const APP_NAME: &str = "Avatar Thrift Store";

#[derive(Clone, Copy, PartialEq)]
pub enum Screen {
    Menu,
    Items,
    ItemDialog,
    Toast,
    Profiles,
}

impl Screen {
    pub const ALL: [Screen; 5] = [
        Screen::Menu,
        Screen::Items,
        Screen::ItemDialog,
        Screen::Toast,
        Screen::Profiles,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Screen::Menu => "Main menu",
            Screen::Items => "Unlock Items",
            Screen::ItemDialog => "Item dialog",
            Screen::Toast => "Unlocked toast",
            Screen::Profiles => "Profiles",
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
enum Body {
    Male,
    Female,
    Other,
}

struct Item {
    name: &'static str,
    installed: bool,
    body: Body,
    error: &'static str,
    // A colour for the made-up preview; 0 for an item without one.
    look: u32,
}

const FOLDERS: [&str; 3] = ["Fable", "Halo Reach", "Holiday Pack"];

const ITEMS: [Item; 10] = [
    Item {
        name: "Blue Hoodie",
        installed: true,
        body: Body::Male,
        error: "",
        look: 0xFF4F7FC0,
    },
    Item {
        name: "Summer Dress With Flowers",
        installed: false,
        body: Body::Female,
        error: "",
        look: 0xFFE0A060,
    },
    Item {
        name: "Knight Armour",
        installed: true,
        body: Body::Male,
        error: "",
        look: 0xFF9AA0A8,
    },
    Item {
        name: "Gold Chain",
        installed: false,
        body: Body::Other,
        error: "",
        look: 0xFFE2C04A,
    },
    Item {
        name: "Hero Coat",
        installed: false,
        body: Body::Female,
        error: "",
        look: 0xFF8A3B3B,
    },
    Item {
        name: "Broken Package",
        installed: false,
        body: Body::Other,
        error: "Not an avatar item",
        look: 0,
    },
    Item {
        name: "Retro Sneakers",
        installed: false,
        body: Body::Male,
        error: "",
        look: 0,
    },
    Item {
        name: "Pirate Hat",
        installed: true,
        body: Body::Female,
        error: "",
        look: 0xFF3A3A44,
    },
    Item {
        name: "Green Scarf",
        installed: false,
        body: Body::Male,
        error: "",
        look: 0xFF5E9A55,
    },
    Item {
        name: "Space Helmet",
        installed: false,
        body: Body::Female,
        error: "",
        look: 0xFFDADDE2,
    },
];

pub struct Options {
    pub folders: bool,
    pub selected_tile: i32,
    pub menu_index: i32,
}

pub fn draw(
    screen: Screen,
    t: &Theme,
    o: &Options,
    font: &[[u16; 32]],
    menu_art: &[Picture],
) -> Vec<u32> {
    let mut c = Canvas::new(WIDTH, HEIGHT, 0xFF000000, font);
    match screen {
        Screen::Menu => draw_menu(&mut c, t, o, menu_art),
        Screen::Items => draw_items(&mut c, t, o),
        Screen::ItemDialog => {
            draw_items(&mut c, t, o);
            let item = selected_item(o);
            let mut lines = vec![item.name.to_string()];
            lines.push(if item.installed {
                "Already unlocked for every profile (Hdd1)".into()
            } else {
                "Not unlocked - unlocks it for every profile".into()
            });
            match item.body {
                Body::Male => lines.push("Male item".into()),
                Body::Female => lines.push("Female item".into()),
                Body::Other => {}
            }
            let options = [
                "Unlock on:  < Hard disk >",
                if item.installed {
                    "Unlock again"
                } else {
                    "Unlock"
                },
                "Cancel",
            ];
            let icon = item_icon(item);
            draw_dialog(&mut c, t, "Avatar item", &lines, &options, 1, Some(&icon));
        }
        Screen::Toast => {
            draw_items(&mut c, t, o);
            let item = selected_item(o);
            let icon = item_icon(item);
            draw_toast(&mut c, t, "Unlocked", item.name, &icon);
        }
        Screen::Profiles => draw_profiles(&mut c, t),
    }
    c.pixels
}

fn selected_item(o: &Options) -> &'static Item {
    &ITEMS[o.selected_tile.clamp(0, ITEMS.len() as i32 - 1) as usize]
}

// main.cpp's NameHash: djb2 over the lower-case name.
fn name_hash(text: &str) -> u32 {
    text.bytes().fold(5381u32, |h, b| {
        h.wrapping_mul(33)
            .wrapping_add(b.to_ascii_lowercase() as u32)
    })
}

fn bold(c: &mut Canvas, x: i32, y: i32, text: &str, color: u32, scale: i32, max_width: i32) {
    c.draw_text(x, y, text, color, scale, max_width);
    c.draw_text(x + 1, y, text, color, scale, max_width);
}

fn text_centered_at(c: &mut Canvas, center_x: i32, y: i32, text: &str, color: u32, max_width: i32) {
    let width = Canvas::text_width(text, 1).min(max_width);
    c.draw_text(center_x - width / 2, y, text, color, 1, max_width);
}

fn wrap(text: &str, max_width: i32) -> Vec<String> {
    let mut lines = Vec::new();
    let mut line = String::new();
    for word in text.split(' ') {
        let longer = if line.is_empty() {
            word.to_string()
        } else {
            format!("{line} {word}")
        };
        if !line.is_empty() && Canvas::text_width(&longer, 1) > max_width {
            lines.push(std::mem::replace(&mut line, word.to_string()));
        } else {
            line = longer;
        }
    }
    if !line.is_empty() {
        lines.push(line);
    }
    lines
}

fn tick(c: &mut Canvas, x: i32, y: i32, unit: i32, color: u32) {
    let th = 3 * unit;
    for i in 0..5 * unit {
        c.fill_rect(x + i, y + 6 * unit + i, th, th, color);
    }
    for i in 0..10 * unit {
        c.fill_rect(x + 5 * unit + i, y + 11 * unit - i, th, th, color);
    }
}

#[allow(clippy::too_many_arguments)]
fn card(
    c: &mut Canvas,
    t: &Theme,
    x: i32,
    mut y: i32,
    w: i32,
    h: i32,
    radius: i32,
    selected: bool,
    lift: bool,
    fill: u32,
) -> i32 {
    if selected && lift {
        y -= 4;
    }
    let drop = if selected && lift { 8 } else { 4 };
    let part = (2 * radius + 2).min(h);
    c.fill_round_rect(
        x - 1,
        y + h + drop + 1 - part,
        w + 2,
        part,
        radius + 1,
        0x30000000,
    );
    c.fill_round_rect(x, y + h + drop - part, w, part, radius, 0x40000000);
    if selected {
        c.fill_round_rect(x - 4, y - 4, w + 8, h + 8, radius + 4, t.green);
    }
    c.fill_round_rect(x, y, w, h, radius, fill);
    y
}

fn background(c: &mut Canvas, t: &Theme) {
    c.fill_gradient(0, 0, WIDTH, HEIGHT, t.sky_top, t.sky_bottom);
    let mut radius = 420;
    while radius > 0 {
        c.fill_circle(260, 20, radius, t.glow);
        radius -= 20;
    }
}

fn header(c: &mut Canvas, t: &Theme, subtitle: &str) {
    background(c, t);
    let title_x = t.margin_x;
    bold(c, title_x + 2, 52, APP_NAME, 0x60000000, 2, 0);
    bold(c, title_x, 50, APP_NAME, t.text, 2, 0);
    if !subtitle.is_empty() {
        let chip_x = title_x + Canvas::text_width(APP_NAME, 2) + 28;
        let chip_w = (Canvas::text_width(subtitle, 1) + 40).min(WIDTH - t.margin_x - chip_x);
        c.fill_round_rect(chip_x, 62, chip_w, 44, 22, t.card);
        c.draw_text(chip_x + 20, 68, subtitle, t.dim, 1, chip_w - 40);
    }
    c.fill_rect(t.margin_x, 128, WIDTH - 2 * t.margin_x, 2, t.line);
}

fn button_glyph(c: &mut Canvas, t: &Theme, x: i32, y: i32, button: &str) -> i32 {
    if button.len() == 1 {
        let color = match button {
            "A" => t.green,
            "B" => t.button_b,
            "X" => t.button_x,
            _ => t.button_y,
        };
        c.fill_circle(x + 18, y + 18, 18, color);
        c.draw_text(x + 10, y + 2, button, t.white, 1, 0);
        c.draw_text(x + 11, y + 2, button, t.white, 1, 0);
        return 36;
    }
    let w = Canvas::text_width(button, 1) + 24;
    c.fill_round_rect(x, y, w, 36, 18, t.button_pill);
    c.draw_text(x + 12, y + 2, button, t.white, 1, 0);
    w
}

fn footer(c: &mut Canvas, t: &Theme, hints: &str) {
    let mut x = t.margin_x;
    let y = t.footer_y;
    for hint in hints.split("   ") {
        let (button, label) = hint.split_once(' ').unwrap_or((hint, ""));
        x += button_glyph(c, t, x, y, button) + 10;
        c.draw_text(x, y + 2, label, t.text, 1, 0);
        x += Canvas::text_width(label, 1) + 36;
    }
}

fn unlocked_mark(c: &mut Canvas, t: &Theme, x: i32, y: i32) {
    c.fill_circle(x + 17, y + 20, 17, 0x30000000);
    c.fill_circle(x + 17, y + 17, 17, t.green);
    tick(c, x + 8, y + 9, 1, t.white);
}

fn body_badge(c: &mut Canvas, t: &Theme, x: i32, y: i32, body: Body) {
    let letter = match body {
        Body::Male => "M",
        Body::Female => "F",
        Body::Other => return,
    };
    c.fill_circle(x + 17, y + 20, 17, 0x30000000);
    c.fill_circle(x + 17, y + 17, 17, t.white);
    bold(
        c,
        x + 17 - Canvas::text_width(letter, 1) / 2,
        y + 1,
        letter,
        t.card,
        1,
        0,
    );
}

fn folder_icon(c: &mut Canvas, t: &Theme, x: i32, y: i32, name: &str) {
    c.fill_round_rect(
        x + 4,
        y + 4,
        56,
        56,
        16,
        t.brights[(name_hash(name) % 5) as usize],
    );
    c.fill_round_rect(x + 16, y + 18, 16, 10, 4, t.white);
    c.fill_round_rect(x + 16, y + 23, 32, 22, 5, t.white);
}

// A made-up preview: a shirt in the item's colour on a transparent square,
// standing in for the package's icon.png.
fn item_icon(item: &Item) -> Vec<u32> {
    if item.look == 0 {
        return Vec::new();
    }
    let font = [[0u16; 32]; 95];
    let mut c = Canvas::new(ICON_SIZE, ICON_SIZE, 0, &font);
    let shade = 0xFF000000 | ((item.look >> 1) & 0x7F7F7F);
    c.fill_round_rect(18, 30, 30, 30, 8, shade);
    c.fill_round_rect(80, 30, 30, 30, 8, shade);
    c.fill_round_rect(36, 26, 56, 82, 10, item.look);
    c.fill_round_rect(52, 22, 24, 12, 6, shade);
    c.pixels
}

fn item_image(c: &mut Canvas, t: &Theme, x: i32, y: i32, size: i32, item: &Item, scaled: &[u32]) {
    if !scaled.is_empty() {
        c.draw_image(x, y, size, size, scaled);
        return;
    }
    let bx = size * 3 / 4;
    c.fill_round_rect(x + (size - bx) / 2, y + (size - bx) / 2, bx, bx, 20, t.key);
    let (mark, color) = if item.error.is_empty() {
        ("?", t.dim)
    } else {
        ("!", t.bad)
    };
    bold(c, x + size / 2 - 16, y + size / 2 - 32, mark, color, 2, 0);
}

fn draw_dialog(
    c: &mut Canvas,
    t: &Theme,
    title: &str,
    lines: &[String],
    options: &[&str],
    selected: i32,
    icon: Option<&[u32]>,
) {
    let band = 64;
    let image_size = 152;
    let w = if icon.is_some() { 940 } else { 860 };
    let mut body = lines.len() as i32 * 40 + 16 + options.len() as i32 * 56;
    if icon.is_some() && body < image_size + 8 {
        body = image_size + 8;
    }
    let h = band + 20 + body + 16;
    let x = (WIDTH - w) / 2;
    let y = (HEIGHT - h) / 2;
    c.fill_rect(0, 0, WIDTH, HEIGHT, t.shade);
    card(c, t, x, y, w, h, 24, false, false, t.card);
    c.fill_round_rect(x, y, w, band, 24, t.green);
    c.fill_rect(x, y + band - 24, w, 24, t.green);
    bold(c, x + 32, y + 16, title, t.white, 1, w - 64);
    let mut left = x + 32;
    if let Some(icon) = icon {
        let (ix, iy) = (x + 28, y + band + 20);
        c.fill_circle(
            ix + image_size / 2,
            iy + image_size / 2,
            image_size / 2,
            t.pastels[0],
        );
        if !icon.is_empty() {
            c.draw_image_scaled(ix, iy, image_size, image_size, icon, ICON_SIZE, ICON_SIZE);
        }
        left = ix + image_size + 28;
    }
    let right = x + w - 24;
    let mut ly = y + band + 20;
    for line in lines {
        c.draw_text(left, ly, line, t.text, 1, right - 8 - left);
        ly += 40;
    }
    ly += 16;
    for (i, option) in options.iter().enumerate() {
        let picked = i as i32 == selected;
        c.fill_round_rect(
            left - 8,
            ly - 7,
            right - left + 8,
            46,
            23,
            if picked { t.green } else { t.key },
        );
        if picked {
            bold(c, left + 16, ly, option, t.white, 1, right - left - 32);
        } else {
            c.draw_text(left + 16, ly, option, t.text, 1, right - left - 32);
        }
        ly += 56;
    }
}

fn scroll_bar(c: &mut Canvas, t: &Theme, content_height: i32, scroll: i32) {
    let view = t.view_bottom - t.list_top;
    if content_height <= view {
        return;
    }
    let x = WIDTH - t.margin_x + 24;
    c.fill_round_rect(x, t.list_top, 8, view, 4, t.line);
    let bar = (view * view / content_height).max(24);
    let top = t.list_top + (view - bar) * scroll / (content_height - view);
    c.fill_round_rect(x, top, 8, bar, 4, t.green);
}

fn draw_toast(c: &mut Canvas, t: &Theme, title: &str, text: &str, icon: &[u32]) {
    let x = t.margin_x - 16;
    let y = t.view_bottom + 4;
    let w = WIDTH - 2 * x;
    let h = 684 - y;
    card(c, t, x, y, w, h, h / 2, false, false, t.green);
    let icon_size = h - 16;
    let icon_x = x + 12;
    c.fill_circle(icon_x + icon_size / 2, y + h / 2, icon_size / 2, t.white);
    if icon.is_empty() {
        tick(c, icon_x + icon_size / 2 - 18, y + h / 2 - 15, 2, t.green);
    } else {
        c.draw_image_scaled(
            icon_x + 4,
            y + 12,
            icon_size - 8,
            icon_size - 8,
            icon,
            ICON_SIZE,
            ICON_SIZE,
        );
    }
    let text_x = icon_x + icon_size + 24;
    let text_y = y + (h - 32) / 2;
    bold(c, text_x, text_y, title, t.white, 1, 0);
    let name_x = text_x + Canvas::text_width(title, 1) + 32;
    c.draw_text(name_x, text_y, text, t.white, 1, x + w - 40 - name_x);
}

fn draw_menu(c: &mut Canvas, t: &Theme, o: &Options, art: &[Picture]) {
    header(c, t, "");
    let options = ["Unlock Items", "Activate Offline Profile", "Avatar Editor"];
    let help = [
        "Unlock items in AvatarItems for every profile",
        "Get an offline profile ready for unlocked items",
        "Go straight to the dashboard's Avatar Editor",
    ];
    let gap = 30;
    let w = (WIDTH - 2 * t.margin_x + 32 - 2 * gap) / 3;
    let h = 420;
    let hero = 190;
    for i in 0..3 {
        let x = t.margin_x - 16 + i as i32 * (w + gap);
        let y = card(
            c,
            t,
            x,
            168,
            w,
            h,
            28,
            i as i32 == o.menu_index,
            true,
            t.card,
        );
        match art.get(i) {
            Some(p) if w > 0 => {
                let pixels = if p.width == w && p.height == hero {
                    p.argb.clone()
                } else {
                    scale_image(&p.argb, p.width, p.height, w, hero)
                };
                c.draw_image(x, y, w, hero, &pixels);
            }
            _ => {
                c.fill_round_rect(x, y, w, hero, 28, t.hero[i]);
                c.fill_rect(x, y + hero - 28, w, 28, t.hero[i]);
            }
        }
        let mut ty = y + hero + 20;
        for line in wrap(options[i], w - 56) {
            bold(c, x + 28, ty, &line, t.text, 1, 0);
            ty += 34;
        }
        ty += 12;
        for line in wrap(help[i], w - 56) {
            c.draw_text(x + 28, ty, &line, t.dim, 1, 0);
            ty += 34;
        }
    }
    footer(c, t, "A select   BACK quit");
}

fn draw_items(c: &mut Canvas, t: &Theme, o: &Options) {
    header(c, t, "Unlock Items");
    let rows = if o.folders { FOLDERS.len() as i32 } else { 0 };
    let selected = rows + o.selected_tile;
    let rect = |index: i32| -> (i32, i32, i32, i32) {
        if index < rows {
            return (
                t.margin_x - 16,
                index * t.row_height,
                WIDTH - 2 * t.margin_x + 32,
                t.row_height,
            );
        }
        let tile = index - rows;
        let x =
            (WIDTH - t.tile_columns * t.tile_width) / 2 + (tile % t.tile_columns) * t.tile_width;
        let y = rows * t.row_height
            + if rows > 0 { 12 } else { 0 }
            + (tile / t.tile_columns) * t.tile_height;
        (x, y, t.tile_width, t.tile_height)
    };
    let total = rows + ITEMS.len() as i32;
    let (_, last_y, _, last_h) = rect(total - 1);
    let content_height = last_y + last_h;
    // Scrolled so the selection shows, as ScrollToShow does from the top.
    let (_, sel_y, _, sel_h) = rect(selected);
    let view = t.view_bottom - t.list_top;
    let scroll = if sel_y + sel_h > view {
        sel_y + sel_h - view
    } else {
        0
    };

    c.set_clip(0, t.list_top - 12, WIDTH, t.view_bottom - t.list_top + 12);
    let text_x = t.margin_x + 64 + 24;
    let text_w = WIDTH - t.margin_x - 72 - text_x;
    for i in 0..total {
        let (x, mut y, w, h) = rect(i);
        y += t.list_top - scroll;
        if y + h < t.list_top - 12 || y > t.view_bottom {
            continue;
        }
        if i >= rows {
            draw_tile(
                c,
                t,
                x,
                y,
                &ITEMS[(i - rows) as usize],
                i == selected,
                i - rows,
            );
            continue;
        }
        let name = FOLDERS[i as usize];
        row_card(c, t, x, y, w, i == selected, true);
        folder_icon(c, t, t.margin_x, y, name);
        c.draw_text(text_x, y + 14, name, t.text, 1, text_w);
    }
    c.reset_clip();
    scroll_bar(c, t, content_height, scroll);

    let item = selected_item(o);
    let mut name_end = WIDTH - t.margin_x;
    if !item.error.is_empty() || item.installed {
        let state = if item.error.is_empty() {
            "Unlocked"
        } else {
            item.error
        };
        let state_w = (Canvas::text_width(state, 1) + 40).min((WIDTH - 2 * t.margin_x) / 2);
        let state_x = WIDTH - t.margin_x - state_w;
        c.fill_round_rect(
            state_x,
            t.info_y - 4,
            state_w,
            40,
            20,
            if item.error.is_empty() {
                t.green
            } else {
                t.bad
            },
        );
        c.draw_text(state_x + 20, t.info_y, state, t.white, 1, state_w - 40);
        name_end = state_x - 32;
    }
    bold(
        c,
        t.margin_x,
        t.info_y,
        item.name,
        t.text,
        1,
        name_end - t.margin_x,
    );
    footer(
        c,
        t,
        "A unlock   B back   X unlock all   Y search   LB/RB page",
    );
}

fn draw_tile(c: &mut Canvas, t: &Theme, x: i32, y: i32, item: &Item, selected: bool, index: i32) {
    let card_x = x + 10;
    let card_w = t.tile_width - 20;
    let top = card(
        c,
        t,
        card_x,
        y + 6,
        card_w,
        t.tile_height - 12,
        18,
        selected,
        true,
        if selected { t.card_picked } else { t.card },
    );
    let image_x = x + (t.tile_width - t.tile_image) / 2;
    let image_y = top + 6;
    if item.error.is_empty() {
        c.fill_circle(
            image_x + t.tile_image / 2,
            image_y + t.tile_image / 2,
            t.tile_image / 2 - 6,
            t.pastels[(index % 5) as usize],
        );
    }
    let icon = item_icon(item);
    let scaled = if icon.is_empty() {
        icon
    } else {
        scale_image(&icon, ICON_SIZE, ICON_SIZE, t.tile_image, t.tile_image)
    };
    item_image(c, t, image_x, image_y, t.tile_image, item, &scaled);
    if item.error.is_empty() && item.installed {
        unlocked_mark(c, t, card_x + 8, top + 8);
    }
    if item.error.is_empty() {
        body_badge(c, t, card_x + card_w - 8 - 34, top + 8, item.body);
    }
    let color = if item.error.is_empty() { t.text } else { t.bad };
    text_centered_at(
        c,
        x + t.tile_width / 2,
        image_y + t.tile_image + 2,
        item.name,
        color,
        card_w - 20,
    );
}

fn row_card(c: &mut Canvas, t: &Theme, x: i32, y: i32, w: i32, selected: bool, chevron: bool) {
    card(
        c,
        t,
        x,
        y - 4,
        w,
        t.row_height - 8,
        18,
        selected,
        false,
        if selected { t.card_picked } else { t.card },
    );
    if chevron {
        bold(
            c,
            x + w - 44,
            y + 14,
            ">",
            if selected { t.green } else { t.dim },
            1,
            0,
        );
    }
}

fn draw_profiles(c: &mut Canvas, t: &Theme) {
    header(c, t, "Profiles");
    // gamertag, drive, XUID line, state, its colour, selected
    let profiles: [(&str, &str, &str, &str, u32); 4] = [
        (
            "PlayerOne",
            "Hdd1",
            "XUID: 00090000AAAAAAAA",
            "Activated - ready for items",
            t.good,
        ),
        (
            "CouchGuest",
            "Hdd1",
            "Offline XUID: E0000123456789AB",
            "Offline - activate it for items",
            t.warn,
        ),
        (
            "LiveFriend",
            "Usb0",
            "XUID: 0009000001234567",
            "Xbox Live - ready for items",
            t.good,
        ),
        ("OldSave", "Usb0", "", "Unreadable: bad header hash", t.bad),
    ];
    c.set_clip(0, t.list_top - 12, WIDTH, t.view_bottom - t.list_top + 12);
    let text_x = t.margin_x + 64 + 24;
    for (i, (gamertag, drive, xuid, state, color)) in profiles.iter().enumerate() {
        let y = t.list_top + i as i32 * t.row_height;
        if y > t.view_bottom {
            continue;
        }
        row_card(
            c,
            t,
            t.margin_x - 16,
            y,
            WIDTH - 2 * t.margin_x + 32,
            i == 1,
            false,
        );
        c.fill_circle(
            t.margin_x + 32,
            y + 31,
            30,
            t.brights[(name_hash(gamertag) % 5) as usize],
        );
        bold(
            c,
            t.margin_x + 24,
            y + 15,
            &gamertag[..1].to_ascii_uppercase(),
            t.white,
            1,
            0,
        );
        let drive_w = Canvas::text_width(drive, 1) + 32;
        let drive_x = WIDTH - t.margin_x - drive_w;
        c.fill_round_rect(drive_x, y + 12, drive_w, 38, 19, t.key);
        c.draw_text(drive_x + 16, y + 15, drive, t.dim, 1, 0);
        bold(c, text_x, y - 2, gamertag, t.text, 1, drive_x - 24 - text_x);
        if !xuid.is_empty() {
            let xuid_x = text_x + Canvas::text_width(gamertag, 1) + 32;
            c.draw_text(xuid_x, y - 2, xuid, t.dim, 1, drive_x - 24 - xuid_x);
        }
        c.draw_text(text_x, y + 30, state, *color, 1, drive_x - 24 - text_x);
    }
    c.reset_clip();
    footer(c, t, "A activate   B back");
}
