// Avatar Thrift Store designer: edit the app's colours and layout (app/theme.h)
// and the main menu cards' pictures (app/menu_art.h) with a live preview of
// its screens, drawn the way the console draws them.
//   cargo run --release [-- --repo <path to avatar_thrift_store>] [--png <dir>]
// Saving rewrites those headers; rebuild the xex to see them on the console.

mod assets;
mod canvas;
mod menu_art;
mod screens;
mod theme;

use eframe::egui;
use egui::color_picker::{Alpha, color_edit_button_hsva};
use egui::ecolor::Hsva;
use menu_art::{BackgroundKind, Object, Tile};
use screens::{Options, Screen};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use theme::Theme;

// The repository: --repo, else the first folder above the current one or the
// executable that has app/theme.h.
fn find_repo() -> Result<PathBuf, String> {
    let args: Vec<String> = std::env::args().collect();
    if let Some(i) = args.iter().position(|a| a == "--repo") {
        let path = PathBuf::from(args.get(i + 1).ok_or("--repo needs a path")?);
        return if path.join("app/theme.h").exists() {
            Ok(path)
        } else {
            Err(format!("{} has no app/theme.h", path.display()))
        };
    }
    let starts = [std::env::current_dir().ok(), std::env::current_exe().ok()];
    for start in starts.into_iter().flatten() {
        for dir in start.ancestors() {
            if dir.join("app/theme.h").exists() {
                return Ok(dir.to_path_buf());
            }
        }
    }
    Err("can't find the repository (a folder with app/theme.h); pass --repo <path>".into())
}

// Writes `text` over `path`, keeping the file's CRLF line endings if it has them.
fn write_keeping_line_endings(path: &Path, text: String) -> std::io::Result<()> {
    let crlf = std::fs::read_to_string(path)
        .map(|t| t.contains("\r\n"))
        .unwrap_or(false);
    std::fs::write(
        path,
        if crlf {
            text.replace('\n', "\r\n")
        } else {
            text
        },
    )
}

// The cards' pictures for these settings. A layer that can't be drawn (an
// image that's missing) is left out of the preview and reported.
fn render_cards(tiles: &[Tile], width: i32, repo: &Path) -> (Vec<assets::Picture>, Vec<String>) {
    let mut pictures = Vec::new();
    let mut errors = Vec::new();
    for (i, tile) in tiles.iter().enumerate() {
        match menu_art::render(tile, width, repo) {
            Ok(p) => pictures.push(p),
            Err(e) => {
                errors.push(format!("{}: {e}", menu_art::NAMES[i]));
                let mut plain = tile.clone();
                if plain.background.kind == BackgroundKind::Image {
                    plain.background.kind = BackgroundKind::Gradient;
                }
                if plain.foreground.object == Object::Image {
                    plain.foreground.object = Object::Nothing;
                }
                pictures.push(
                    menu_art::render(&plain, width, repo)
                        .expect("a card without images always draws"),
                );
            }
        }
    }
    (pictures, errors)
}

struct Designer {
    repo: PathBuf,
    font: Vec<[u16; 32]>,
    theme: Theme,
    saved: Theme,
    notes: Vec<String>,
    tiles: Vec<Tile>,
    saved_tiles: Vec<Tile>,
    // The width of the pictures in menu_art.h; they're redrawn when the cards' width changes.
    saved_art_width: i32,
    art: Vec<assets::Picture>,
    art_errors: Vec<String>,
    art_drawn: Option<(Vec<Tile>, i32)>,
    card: usize,
    status: String,
    screen: Screen,
    folders: bool,
    selected_tile: i32,
    menu_index: i32,
    hex: HashMap<String, String>,
    texture: Option<egui::TextureHandle>,
    pixels: Vec<u32>,
    drawn: Option<(Theme, Vec<Tile>, Screen, bool, i32, i32)>,
}

impl Designer {
    fn new(repo: PathBuf) -> Result<Self, String> {
        let font = assets::load_font(&repo.join("app"))?;
        let shipped: Vec<Tile> = (0..3).map(Tile::shipped).collect();
        let mut d = Designer {
            repo,
            font,
            theme: Theme::default(),
            saved: Theme::default(),
            notes: Vec::new(),
            tiles: shipped.clone(),
            saved_tiles: shipped,
            saved_art_width: menu_art::card_width(80),
            art: Vec::new(),
            art_errors: Vec::new(),
            art_drawn: None,
            card: 0,
            status: String::new(),
            screen: Screen::Items,
            folders: true,
            selected_tile: 1,
            menu_index: 0,
            hex: HashMap::new(),
            texture: None,
            pixels: Vec::new(),
            drawn: None,
        };
        d.reload();
        Ok(d)
    }

    fn theme_path(&self) -> PathBuf {
        self.repo.join("app/theme.h")
    }

    fn art_path(&self) -> PathBuf {
        self.repo.join("app/menu_art.h")
    }

    fn reload(&mut self) {
        match std::fs::read_to_string(self.theme_path()) {
            Ok(text) => {
                let (theme, notes) = Theme::parse(&text);
                self.theme = theme.clone();
                self.saved = theme;
                self.notes = notes;
                self.status = format!("Read {}", self.repo.join("app").display());
            }
            Err(e) => self.status = format!("Can't read theme.h: {e}"),
        }
        // menu_art.h from before the designer has no settings: its pictures
        // are the shipped cards (made by the old tools/menu_art.html).
        let header = std::fs::read_to_string(self.art_path()).unwrap_or_default();
        let (tiles, width) = match menu_art::read_settings(&header) {
            Some(found) => found,
            None => {
                let width = assets::load_menu_art(&self.repo.join("app"))
                    .first()
                    .map(|p| p.width)
                    .unwrap_or(menu_art::card_width(80));
                self.notes
                    .push("menu_art.h has no designer settings: the cards start as shipped".into());
                ((0..3).map(Tile::shipped).collect(), width)
            }
        };
        self.tiles = tiles.clone();
        self.saved_tiles = tiles;
        self.saved_art_width = width;
        self.hex.clear();
    }

    fn art_width(&self) -> i32 {
        menu_art::card_width(self.theme.margin_x)
    }

    fn art_dirty(&self) -> bool {
        self.tiles != self.saved_tiles || self.art_width() != self.saved_art_width
    }

    fn save(&mut self) {
        let mut written = Vec::new();
        if self.theme != self.saved {
            if let Err(e) = write_keeping_line_endings(&self.theme_path(), self.theme.to_header()) {
                self.status = format!("Can't write theme.h: {e}");
                return;
            }
            self.saved = self.theme.clone();
            written.push("app/theme.h");
        }
        if self.art_dirty() {
            let width = self.art_width();
            let (pictures, errors) = render_cards(&self.tiles, width, &self.repo);
            if !errors.is_empty() {
                self.status = format!("Not saving the cards: {}", errors.join("; "));
                return;
            }
            let pngs: Result<Vec<Vec<u8>>, String> =
                pictures.iter().map(menu_art::encode_png).collect();
            let result = pngs.and_then(|pngs| {
                let header = menu_art::write_header(&self.tiles, width, &pngs);
                write_keeping_line_endings(&self.art_path(), header).map_err(|e| e.to_string())
            });
            if let Err(e) = result {
                self.status = format!("Can't write menu_art.h: {e}");
                return;
            }
            self.saved_tiles = self.tiles.clone();
            self.saved_art_width = width;
            written.push("app/menu_art.h");
        }
        self.status = format!(
            "Saved {}. Rebuild the xex (build_xbox.ps1) to try it on the console.",
            written.join(" and ")
        );
    }

    fn export(&mut self) {
        let dir = self.repo.join("build/designer");
        let name = self.screen.label().to_lowercase().replace(' ', "_");
        let path = dir.join(format!("{name}.png"));
        let result = std::fs::create_dir_all(&dir)
            .map_err(|e| e.to_string())
            .and_then(|_| {
                assets::write_png(
                    &path,
                    canvas::WIDTH as u32,
                    canvas::HEIGHT as u32,
                    &self.pixels,
                )
            });
        self.status = match result {
            Ok(()) => format!("Wrote {}", path.display()),
            Err(e) => format!("Can't write the PNG: {e}"),
        };
    }

    fn options(&self) -> Options {
        Options {
            folders: self.folders,
            selected_tile: self.selected_tile,
            menu_index: self.menu_index,
        }
    }

    // Draws the cards' pictures again when their settings or width changed.
    fn refresh_art(&mut self) {
        let key = (self.tiles.clone(), self.art_width());
        if self.art_drawn.as_ref() == Some(&key) {
            return;
        }
        (self.art, self.art_errors) = render_cards(&self.tiles, key.1, &self.repo);
        self.art_drawn = Some(key);
    }

    // Draws the preview again when anything it shows has changed.
    fn refresh(&mut self, ctx: &egui::Context) {
        let key = (
            self.theme.clone(),
            self.tiles.clone(),
            self.screen,
            self.folders,
            self.selected_tile,
            self.menu_index,
        );
        if self.drawn.as_ref() == Some(&key) && self.texture.is_some() {
            return;
        }
        if self.screen == Screen::Menu {
            self.refresh_art();
        }
        self.pixels = screens::draw(
            self.screen,
            &self.theme,
            &self.options(),
            &self.font,
            &self.art,
        );
        let rgba: Vec<u8> = self
            .pixels
            .iter()
            .flat_map(|p| [(p >> 16) as u8, (p >> 8) as u8, *p as u8, 0xFF])
            .collect();
        let image = egui::ColorImage::from_rgba_unmultiplied(
            [canvas::WIDTH as usize, canvas::HEIGHT as usize],
            &rgba,
        );
        match &mut self.texture {
            Some(t) => t.set(image, egui::TextureOptions::LINEAR),
            None => {
                self.texture =
                    Some(ctx.load_texture("preview", image, egui::TextureOptions::LINEAR))
            }
        }
        self.drawn = Some(key);
    }
}

// A colour swatch with a picker, and its ARGB value as hex text.
fn color_row(
    ui: &mut egui::Ui,
    hex: &mut HashMap<String, String>,
    key: String,
    label: &str,
    help: &str,
    value: &mut u32,
) {
    ui.horizontal(|ui| {
        let v = *value;
        let mut hsva = Hsva::from_srgba_unmultiplied([
            (v >> 16) as u8,
            (v >> 8) as u8,
            v as u8,
            (v >> 24) as u8,
        ]);
        if color_edit_button_hsva(ui, &mut hsva, Alpha::OnlyBlend).changed() {
            let [r, g, b, a] = hsva.to_srgba_unmultiplied();
            *value = (a as u32) << 24 | (r as u32) << 16 | (g as u32) << 8 | b as u32;
        }
        let id = ui.make_persistent_id(&key);
        let buffer = hex.entry(key).or_default();
        if !ui.memory(|m| m.has_focus(id)) {
            *buffer = format!("{:08X}", *value);
        }
        let edit = ui.add(
            egui::TextEdit::singleline(buffer)
                .id(id)
                .desired_width(76.0)
                .font(egui::TextStyle::Monospace),
        );
        if edit.changed()
            && buffer.len() == 8
            && let Ok(n) = u32::from_str_radix(buffer, 16)
        {
            *value = n;
        }
        let name = ui.label(label);
        if !help.is_empty() {
            name.on_hover_text(help);
        }
    });
}

// An image file's path, with a button that picks one. Paths inside the
// repository are kept relative to it.
fn image_row(ui: &mut egui::Ui, repo: &Path, path: &mut String) {
    ui.horizontal(|ui| {
        ui.add(
            egui::TextEdit::singleline(path)
                .desired_width(220.0)
                .hint_text("PNG or JPEG"),
        );
        if ui.button("Choose...").clicked()
            && let Some(file) = rfd::FileDialog::new()
                .add_filter("Images", &["png", "jpg", "jpeg"])
                .set_directory(repo)
                .pick_file()
        {
            *path = match file.strip_prefix(repo) {
                Ok(inside) => inside.to_string_lossy().replace('\\', "/"),
                Err(_) => file.to_string_lossy().into_owned(),
            };
        }
    });
}

// One card's two layers.
fn card_editor(
    ui: &mut egui::Ui,
    hex: &mut HashMap<String, String>,
    repo: &Path,
    index: usize,
    tile: &mut Tile,
) {
    let b = &mut tile.background;
    ui.strong("Background");
    ui.horizontal(|ui| {
        ui.radio_value(&mut b.kind, BackgroundKind::Gradient, "Gradient");
        ui.radio_value(&mut b.kind, BackgroundKind::Image, "Image file");
    });
    // The gradient also shows through an image's transparent parts.
    if b.kind == BackgroundKind::Image {
        image_row(ui, repo, &mut b.image);
        ui.label("Under its transparent parts:");
    }
    color_row(
        ui,
        hex,
        format!("card{index}.light"),
        "top",
        "",
        &mut b.light,
    );
    color_row(
        ui,
        hex,
        format!("card{index}.base"),
        "middle",
        "",
        &mut b.base,
    );
    color_row(
        ui,
        hex,
        format!("card{index}.dark"),
        "bottom",
        "",
        &mut b.dark,
    );
    ui.horizontal(|ui| {
        ui.checkbox(&mut b.stripes, "stripes");
        ui.checkbox(&mut b.glow, "glow");
    });

    let f = &mut tile.foreground;
    ui.add_space(6.0);
    ui.strong("Object in front");
    let before = f.object;
    egui::ComboBox::from_id_salt(("object", index))
        .selected_text(f.object.label())
        .show_ui(ui, |ui| {
            for o in Object::ALL {
                ui.selectable_value(&mut f.object, o, o.label());
            }
        });
    // A drawing still in its own colours takes the new one's own colours.
    if f.object != before && (f.top, f.bottom) == menu_art::default_fill(before) {
        (f.top, f.bottom) = menu_art::default_fill(f.object);
    }
    match f.object {
        Object::Image => image_row(ui, repo, &mut f.image),
        Object::Nothing => {}
        _ => {
            color_row(
                ui,
                hex,
                format!("card{index}.top"),
                "fill, top",
                "",
                &mut f.top,
            );
            color_row(
                ui,
                hex,
                format!("card{index}.bottom"),
                "fill, bottom",
                "",
                &mut f.bottom,
            );
        }
    }
    if f.object != Object::Nothing {
        ui.add(egui::Slider::new(&mut f.scale, 0.3..=2.0).text("size"));
        ui.add(egui::Slider::new(&mut f.x, -200.0..=200.0).text("left / right"));
        ui.add(egui::Slider::new(&mut f.y, -100.0..=100.0).text("up / down"));
        ui.checkbox(&mut f.shadow, "shadow");
    }
    ui.checkbox(&mut f.sparkles, "sparkles");
    ui.add_space(4.0);
    if ui.button("Reset this card").clicked() {
        *tile = Tile::shipped(index);
    }
}

impl eframe::App for Designer {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        self.refresh(&ctx);
        let (errors, warnings) = self.theme.check();
        let dirty = self.theme != self.saved || self.art_dirty();

        egui::Panel::top("toolbar").show(ui, |ui| {
            ui.horizontal(|ui| {
                let save = ui
                    .add_enabled(
                        dirty && errors.is_empty() && self.art_errors.is_empty(),
                        egui::Button::new("Save"),
                    )
                    .on_hover_text("Writes app/theme.h and app/menu_art.h, where they changed");
                if save.clicked() {
                    self.save();
                }
                if ui
                    .button("Reload")
                    .on_hover_text(
                        "Read app/theme.h and app/menu_art.h again, dropping unsaved changes",
                    )
                    .clicked()
                {
                    self.reload();
                }
                if ui
                    .button("Defaults")
                    .on_hover_text(
                        "The design the app shipped with, cards included (not saved yet)",
                    )
                    .clicked()
                {
                    self.theme = Theme::default();
                    self.tiles = (0..3).map(Tile::shipped).collect();
                    self.hex.clear();
                }
                if ui
                    .button("Export PNG")
                    .on_hover_text("This preview, to build/designer/")
                    .clicked()
                {
                    self.export();
                }
                ui.separator();
                ui.label(if dirty { "unsaved changes" } else { "saved" });
                ui.separator();
                ui.label(&self.status);
            });
        });

        egui::Panel::left("settings")
            .resizable(true)
            .default_size(400.0)
            .show(ui, |ui| {
                egui::ScrollArea::vertical().show(ui, |ui| {
                    ui.heading("Screen");
                    for s in Screen::ALL {
                        ui.radio_value(&mut self.screen, s, s.label());
                    }
                    match self.screen {
                        Screen::Menu => {
                            ui.add(
                                egui::Slider::new(&mut self.menu_index, 0..=2)
                                    .text("selected card"),
                            );
                        }
                        Screen::Profiles => {}
                        _ => {
                            ui.checkbox(&mut self.folders, "folders above the items");
                            ui.add(
                                egui::Slider::new(&mut self.selected_tile, 0..=9)
                                    .text("selected item"),
                            );
                        }
                    }

                    let red = egui::Color32::from_rgb(0xF0, 0x70, 0x60);
                    if !errors.is_empty()
                        || !warnings.is_empty()
                        || !self.notes.is_empty()
                        || !self.art_errors.is_empty()
                    {
                        ui.separator();
                        ui.heading("Checks");
                        for e in &errors {
                            ui.colored_label(red, format!("Error: {e}"));
                        }
                        for e in &self.art_errors {
                            ui.colored_label(red, format!("Error: {e}"));
                        }
                        for w in &warnings {
                            ui.colored_label(egui::Color32::from_rgb(0xE3, 0xAE, 0x4F), w);
                        }
                        for n in &self.notes {
                            ui.label(n);
                        }
                        if !errors.is_empty() || !self.art_errors.is_empty() {
                            ui.label("Saving is off until the errors are fixed.");
                        }
                    }

                    let Designer {
                        theme,
                        hex,
                        tiles,
                        card,
                        screen,
                        menu_index,
                        repo,
                        ..
                    } = self;
                    ui.separator();
                    egui::CollapsingHeader::new("Menu cards")
                        .default_open(true)
                        .show(ui, |ui| {
                            ui.horizontal(|ui| {
                                for (i, name) in menu_art::NAMES.iter().enumerate() {
                                    let short = name.split(' ').next().unwrap_or(name);
                                    if ui.selectable_label(*card == i, short).on_hover_text(*name).clicked() {
                                        *card = i;
                                        *screen = Screen::Menu;
                                        *menu_index = i as i32;
                                    }
                                }
                            });
                            card_editor(ui, hex, repo, *card, &mut tiles[*card]);
                            ui.small(
                                "Images go into app/menu_art.h, which is committed: use only images you may share.",
                            );
                        });
                    egui::CollapsingHeader::new("Colours")
                        .default_open(true)
                        .show(ui, |ui| {
                            for f in theme.colors() {
                                color_row(ui, hex, f.name.to_string(), f.name, f.help, f.value);
                            }
                        });
                    egui::CollapsingHeader::new("Palettes")
                        .default_open(true)
                        .show(ui, |ui| {
                            for list in theme.color_lists() {
                                ui.label(list.name).on_hover_text(list.help);
                                for (i, v) in list.values.iter_mut().enumerate() {
                                    let label = format!("{}[{i}]", list.name);
                                    color_row(ui, hex, label.clone(), &label, "", v);
                                }
                            }
                        });
                    egui::CollapsingHeader::new("Layout (pixels on the 1280 x 720 screen)")
                        .default_open(true)
                        .show(ui, |ui| {
                            egui::Grid::new("layout").num_columns(2).show(ui, |ui| {
                                for f in theme.layout() {
                                    let name = ui.label(f.name);
                                    if !f.help.is_empty() {
                                        name.on_hover_text(f.help);
                                    }
                                    ui.add(
                                        egui::DragValue::new(f.value).range(f.range.0..=f.range.1),
                                    );
                                    ui.end_row();
                                }
                            });
                        });
                });
            });

        egui::CentralPanel::default().show(ui, |ui| {
            if let Some(texture) = &self.texture {
                let avail = ui.available_size();
                let scale = (avail.x / canvas::WIDTH as f32).min(avail.y / canvas::HEIGHT as f32);
                let size = egui::vec2(canvas::WIDTH as f32, canvas::HEIGHT as f32) * scale;
                ui.centered_and_justified(|ui| {
                    ui.image((texture.id(), size));
                });
            }
        });
    }
}

fn main() -> eframe::Result {
    let repo = match find_repo() {
        Ok(r) => r,
        Err(e) => {
            eprintln!("designer: {e}");
            std::process::exit(1);
        }
    };
    let mut designer = match Designer::new(repo) {
        Ok(d) => d,
        Err(e) => {
            eprintln!("designer: {e}");
            std::process::exit(1);
        }
    };
    // --png <dir>: every screen with the saved design, as PNGs, without a window.
    let args: Vec<String> = std::env::args().collect();
    if let Some(i) = args.iter().position(|a| a == "--png") {
        let dir = PathBuf::from(args.get(i + 1).map(String::as_str).unwrap_or("."));
        designer.refresh_art();
        for e in &designer.art_errors {
            eprintln!("designer: {e}");
        }
        for screen in Screen::ALL {
            let pixels = screens::draw(
                screen,
                &designer.theme,
                &designer.options(),
                &designer.font,
                &designer.art,
            );
            let name = screen.label().to_lowercase().replace(' ', "_");
            let path = dir.join(format!("{name}.png"));
            if let Err(e) =
                assets::write_png(&path, canvas::WIDTH as u32, canvas::HEIGHT as u32, &pixels)
            {
                eprintln!("designer: {}: {e}", path.display());
                std::process::exit(1);
            }
            println!("{}", path.display());
        }
        return Ok(());
    }
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("Avatar Thrift Store designer")
            .with_inner_size([1720.0, 900.0]),
        ..Default::default()
    };
    eframe::run_native(
        "Avatar Thrift Store designer",
        options,
        Box::new(|_| Ok(Box::new(designer))),
    )
}
