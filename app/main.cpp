// Avatar Thrift Store - xsdk homebrew for modded consoles.
//
// "Unlock Items": browses the AvatarItems folder next to the app (folders as a
// list, items as tiles with their previews, a search over all folder names)
// and unlocks items for the shared online XUID (patched, then copied to
// Content\0000000000000000\AAAAAAAA\00009000 on the hard disk or a USB stick,
// picked in the dialog; the app's own drive first).
// "Activate Offline Profile": gives an offline profile that XUID, the way
// 360mpGui's offline-to-online maker does, and re-signs it with this
// console's own key. The README's "Key findings" describe the formats behind both.
//
// Controls: D-pad/left stick to move, LB/RB a page, A to open or unlock, B to go back,
// Y to search, BACK to quit.

#include "gfx.h"
#include "items.h"
#include "menu_art.h"
#include "profiles.h"
#include "screenshot.h"
#include "../core/account.h"
#include "stb_image.h"

#include "xsdk/kernel.h"
#include "xsdk/log.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <string>
#include <vector>

// A cosy store at night: dark forest-green surfaces with cream text, muted
// spotlights behind the items, sage and amber accents.
static const u32 kColorSkyTop      = 0xFF223029;
static const u32 kColorSkyBottom   = 0xFF141B18;
static const u32 kColorCard        = 0xFF2A3631;   // rows, dialogs, menu cards
static const u32 kColorCardPicked  = 0xFF33413A;
static const u32 kColorGreen       = 0xFF6FA35A;
static const u32 kColorGreenDark   = 0xFF4E7A40;
static const u32 kColorYellow      = 0xFFE3AE4F;
static const u32 kColorWhite       = 0xFFFAF5EC;
static const u32 kColorText        = 0xFFECE5D8;   // on dark surfaces
static const u32 kColorDim         = 0xFF98A39C;
static const u32 kColorInk         = 0xFF2E2A25;   // on amber
static const u32 kColorGood        = 0xFF8CC475;
static const u32 kColorWarn        = 0xFFE3AE4F;
static const u32 kColorBad         = 0xFFE07A5F;
static const u32 kColorKey         = 0xFF36443E;
static const u32 kColorShade       = 0x80000000;   // behind dialogs

// Spotlights behind item previews, and icon colors picked by name.
static const u32 kPastels[5] = { 0xFF3D5245, 0xFF3A4A55, 0xFF4D4238, 0xFF453F52, 0xFF4C4A36 };
static const u32 kBrights[5] = { 0xFF6FA35A, 0xFF5B8DB8, 0xFFC9824F, 0xFF8C76B0, 0xFFC9A44A };

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

static const char* kAppName = "Avatar Thrift Store";
static const int kMenuCards = 3;
static const u32 kToastMs = 2500;
static const size_t kMaxQuery = 30;

// ---------------------------------------------------------------------------
// Input

enum Button
{
    kButtonUp = INPUT_GAMEPAD_DPAD_UP,
    kButtonDown = INPUT_GAMEPAD_DPAD_DOWN,
    kButtonLeft = INPUT_GAMEPAD_DPAD_LEFT,
    kButtonRight = INPUT_GAMEPAD_DPAD_RIGHT,
    kButtonStart = INPUT_GAMEPAD_START,
    kButtonBack = INPUT_GAMEPAD_BACK,
    kButtonLB = INPUT_GAMEPAD_LEFT_SHOULDER,
    kButtonRB = INPUT_GAMEPAD_RIGHT_SHOULDER,
    kButtonA = INPUT_GAMEPAD_A,
    kButtonB = INPUT_GAMEPAD_B,
    kButtonX = INPUT_GAMEPAD_X,
    kButtonY = INPUT_GAMEPAD_Y,
};

static const u16 kRepeating = kButtonUp | kButtonDown | kButtonLeft | kButtonRight | kButtonLB | kButtonRB | kButtonX;

static u32 NowMs()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

// Buttons newly pressed since the last poll; directions, the shoulders and X
// (delete on the keyboard) repeat while held.
class Pad
{
public:
    Pad() : m_held(0), m_repeatAt(0), m_triggersHeld(false), m_screenshot(false) {}

    // True once when both triggers have just been pulled, the screenshot
    // shortcut (the app uses the triggers for nothing else).
    bool ScreenshotPressed() const { return m_screenshot; }

    u16 Poll()
    {
        INPUT_STATE state;
        u16 buttons = 0;
        bool triggers = false;
        if (XamInputGetState(0, 0, &state) == ERROR_SUCCESS)
        {
            triggers = state.gamepad.left_trigger > 128 && state.gamepad.right_trigger > 128;
            buttons = state.gamepad.buttons;
            if (state.gamepad.thumb_ly > 16000)
                buttons |= kButtonUp;
            if (state.gamepad.thumb_ly < -16000)
                buttons |= kButtonDown;
            if (state.gamepad.thumb_lx < -16000)
                buttons |= kButtonLeft;
            if (state.gamepad.thumb_lx > 16000)
                buttons |= kButtonRight;
        }
        u16 pressed = buttons & ~m_held;
        u32 now = NowMs();
        if (pressed & kRepeating)
            m_repeatAt = now + 400;
        else if ((buttons & kRepeating) && now >= m_repeatAt)
        {
            pressed |= buttons & kRepeating;
            m_repeatAt = now + 90;
        }
        m_held = buttons;
        m_screenshot = triggers && !m_triggersHeld;
        m_triggersHeld = triggers;
        return pressed;
    }

private:
    u16 m_held;
    u32 m_repeatAt;
    bool m_triggersHeld;
    bool m_screenshot;
};

// ---------------------------------------------------------------------------
// Drawing helpers

static u32 NameHash(const std::string& text)
{
    u32 hash = 5381;
    for (size_t i = 0; i < text.size(); i++)
        hash = hash * 33 + (u32)tolower((unsigned char)text[i]);
    return hash;
}

// The font has one weight; drawing it twice, a pixel apart, makes it bold.
static void DrawBoldText(Canvas& c, int x, int y, const std::string& text, u32 color, int scale = 1, int maxWidth = 0)
{
    c.DrawText(x, y, text, color, scale, maxWidth);
    c.DrawText(x + 1, y, text, color, scale, maxWidth);
}

// Text centred on `centerX`, cut to `maxWidth`.
static void DrawTextCenteredAt(Canvas& c, int centerX, int y, const std::string& text, u32 color, int maxWidth)
{
    int width = Canvas::TextWidth(text);
    width = width < maxWidth ? width : maxWidth;
    c.DrawText(centerX - width / 2, y, text, color, 1, maxWidth);
}

// Splits `text` at spaces into lines no wider than `maxWidth`.
static std::vector<std::string> WrapText(const std::string& text, int maxWidth)
{
    std::vector<std::string> lines;
    std::string line;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(' ', start);
        if (end == std::string::npos)
            end = text.size();
        std::string word = text.substr(start, end - start);
        std::string longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && Canvas::TextWidth(longer) > maxWidth)
        {
            lines.push_back(line);
            line = word;
        }
        else
            line = longer;
        start = end + 1;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

// A tick in a box of about 15 x 11 `unit`s.
static void DrawTick(Canvas& c, int x, int y, int unit, u32 color)
{
    int t = 3 * unit;
    for (int i = 0; i < 5 * unit; i++)
        c.FillRect(x + i, y + 6 * unit + i, t, t, color);
    for (int i = 0; i < 10 * unit; i++)
        c.FillRect(x + 5 * unit + i, y + 11 * unit - i, t, t, color);
}

// A card with a soft shadow. A selected one gets a green rim and, with
// `lift`, rises a little; returns the card's top as drawn.
static int DrawCard(Canvas& c, int x, int y, int w, int h, int radius, bool selected, bool lift, u32 fill = kColorCard)
{
    if (selected && lift)
        y -= 4;
    // The shadow shows only below the card, so only its bottom part is drawn
    // (blending the whole card area twice is slow on the console).
    int drop = selected && lift ? 8 : 4;
    int part = 2 * radius + 2;
    part = part < h ? part : h;
    c.FillRoundRect(x - 1, y + h + drop + 1 - part, w + 2, part, radius + 1, 0x30000000);
    c.FillRoundRect(x, y + h + drop - part, w, part, radius, 0x40000000);
    if (selected)
        c.FillRoundRect(x - 4, y - 4, w + 8, h + 8, radius + 4, kColorGreen);
    c.FillRoundRect(x, y, w, h, radius, fill);
    return y;
}

// Background: a dark gradient with a faint warm glow behind the title, drawn
// once, then copied.
static void DrawBackground(Canvas& c)
{
    static std::vector<u32> s_background;
    if (!s_background.empty())
    {
        c.LoadFrom(s_background);
        return;
    }
    c.FillGradient(0, 0, c.Width(), c.Height(), kColorSkyTop, kColorSkyBottom);
    // Many faint discs, smaller towards the middle, so the glow fades out smoothly.
    for (int radius = 420; radius > 0; radius -= 20)
        c.FillCircle(260, 20, radius, 0x02F3D9A8);
    c.SaveTo(&s_background);
}

// A shopping bag, 56 x 60.
static void DrawBagIcon(Canvas& c, int x, int y)
{
    c.FillRoundRect(x + 14, y, 28, 30, 14, kColorGreenDark);
    c.FillRoundRect(x + 19, y + 5, 18, 26, 9, kColorSkyTop);
    c.FillRoundRect(x + 2, y + 18, 52, 44, 10, 0x40000000);
    c.FillRoundRect(x, y + 14, 56, 46, 10, kColorGreen);
    c.FillRoundRect(x + 14, y + 26, 28, 6, 3, kColorGreenDark);
}

static void DrawHeader(Canvas& c, const std::string& subtitle)
{
    DrawBackground(c);
    DrawBagIcon(c, kMarginX, 54);
    int titleX = kMarginX + 76;
    DrawBoldText(c, titleX + 2, 52, kAppName, 0x60000000, 2);
    DrawBoldText(c, titleX, 50, kAppName, kColorText, 2);
    // The current place, if any, on a small label.
    if (!subtitle.empty())
    {
        int chipX = titleX + Canvas::TextWidth(kAppName, 2) + 28;
        int chipW = Canvas::TextWidth(subtitle) + 40;
        int maxW = c.Width() - kMarginX - chipX;
        chipW = chipW < maxW ? chipW : maxW;
        c.FillRoundRect(chipX, 62, chipW, 44, 22, kColorCard);
        c.DrawText(chipX + 20, 68, subtitle, kColorDim, 1, chipW - 40);
    }
    c.FillRect(kMarginX, 128, c.Width() - 2 * kMarginX, 2, 0x1AF3ECE0);
}

// A controller button: a coloured disc for A/B/X/Y, a grey pill for the rest.
// Returns its width.
static int DrawButtonGlyph(Canvas& c, int x, int y, const std::string& button)
{
    if (button.size() == 1)
    {
        u32 color = button == "A" ? kColorGreen : button == "B" ? 0xFFC8604F : button == "X" ? 0xFF5B8DB8 : 0xFFD9A441;
        c.FillCircle(x + 18, y + 18, 18, color);
        c.DrawText(x + 10, y + 2, button, kColorWhite);
        c.DrawText(x + 11, y + 2, button, kColorWhite);
        return 36;
    }
    int w = Canvas::TextWidth(button) + 24;
    c.FillRoundRect(x, y, w, 36, 18, 0xFF4A5751);
    c.DrawText(x + 12, y + 2, button, kColorWhite);
    return w;
}

// Hints such as "A choose   B back": three spaces apart, each starting with its button.
static void DrawHints(Canvas& c, int x, int y, const std::string& hints)
{
    size_t start = 0;
    while (start < hints.size())
    {
        size_t end = hints.find("   ", start);
        if (end == std::string::npos)
            end = hints.size();
        std::string hint = hints.substr(start, end - start);
        size_t space = hint.find(' ');
        std::string label = space == std::string::npos ? "" : hint.substr(space + 1);
        x += DrawButtonGlyph(c, x, y, hint.substr(0, space)) + 10;
        c.DrawText(x, y + 2, label, kColorText);
        x += Canvas::TextWidth(label) + 36;
        start = end + 3;
    }
}

static void DrawFooter(Canvas& c, const std::string& hints)
{
    DrawHints(c, kMarginX, kFooterY, hints);
}

// The tag on a tile: "NEW" until the item is unlocked, then "UNLOCKED".
static void DrawTag(Canvas& c, int x, int y, bool unlocked)
{
    std::string text = unlocked ? "UNLOCKED" : "NEW";
    int w = Canvas::TextWidth(text) + 24 + (unlocked ? 26 : 0);
    c.FillRoundRect(x, y + 3, w, 34, 17, 0x30000000);
    c.FillRoundRect(x, y, w, 34, 17, unlocked ? kColorGreen : kColorYellow);
    int textX = x + 12;
    if (unlocked)
    {
        DrawTick(c, textX, y + 10, 1, kColorWhite);
        textX += 26;
    }
    DrawBoldText(c, textX, y + 1, text, unlocked ? kColorWhite : kColorInk);
}

// A folder on a coloured rounded square, in a 64 x 64 cell.
static void DrawFolderIcon(Canvas& c, int x, int y, const std::string& name)
{
    c.FillRoundRect(x + 4, y + 4, 56, 56, 16, kBrights[NameHash(name) % 5]);
    c.FillRoundRect(x + 16, y + 18, 16, 10, 4, kColorWhite);
    c.FillRoundRect(x + 16, y + 23, 32, 22, 5, kColorWhite);
}

// A white disc with a green plus, the "Unlock all" row, in a 64 x 64 cell.
static void DrawAddAllIcon(Canvas& c, int x, int y)
{
    c.FillCircle(x + 32, y + 32, 28, kColorWhite);
    c.FillRoundRect(x + 28, y + 14, 8, 36, 4, kColorGreen);
    c.FillRoundRect(x + 14, y + 28, 36, 8, 4, kColorGreen);
}

// The preview, already scaled to `size` (`scaled`), or a placeholder without one.
static void DrawItemImage(Canvas& c, int x, int y, int size, const Item& item, const std::vector<u32>& scaled)
{
    if (!scaled.empty())
    {
        c.DrawImage(x, y, size, size, &scaled[0]);
        return;
    }
    if (!item.detailsLoaded)
        return;   // still loading: just the spotlight for now
    int box = size * 3 / 4;
    int bx = x + (size - box) / 2;
    int by = y + (size - box) / 2;
    c.FillRoundRect(bx, by, box, box, 20, kColorKey);
    const char* mark = item.error.empty() ? "?" : "!";
    DrawBoldText(c, x + size / 2 - 16, y + size / 2 - 32, mark, item.error.empty() ? kColorDim : kColorBad, 2);
}

// A centred card with a green title band, text lines and pill-shaped options,
// over the dimmed screen; with `icon`, the item's preview sits on the left.
static void DrawDialog(Canvas& c, const std::string& title, const std::vector<std::string>& lines,
                       const std::vector<std::string>& options, int selected, const std::vector<u32>* icon = NULL)
{
    const int band = 64;
    const int imageSize = 152;
    bool image = icon != NULL;
    int w = image ? 940 : 860;
    int body = (int)lines.size() * 40 + 16 + (int)options.size() * 56;
    if (image && body < imageSize + 8)
        body = imageSize + 8;
    int h = band + 20 + body + 16;
    int x = (c.Width() - w) / 2;
    int y = (c.Height() - h) / 2;
    c.FillRect(0, 0, c.Width(), c.Height(), kColorShade);
    DrawCard(c, x, y, w, h, 24, false, false);
    c.FillRoundRect(x, y, w, band, 24, kColorGreen);
    c.FillRect(x, y + band - 24, w, 24, kColorGreen);
    DrawBoldText(c, x + 32, y + 16, title, kColorWhite, 1, w - 64);

    int left = x + 32;
    if (image)
    {
        int ix = x + 28;
        int iy = y + band + 20;
        c.FillCircle(ix + imageSize / 2, iy + imageSize / 2, imageSize / 2, kPastels[0]);
        if (!icon->empty())
            c.DrawImageScaled(ix, iy, imageSize, imageSize, &(*icon)[0], kIconSize, kIconSize);
        left = ix + imageSize + 28;
    }
    int right = x + w - 24;
    int ly = y + band + 20;
    for (size_t i = 0; i < lines.size(); i++, ly += 40)
        c.DrawText(left, ly, lines[i], kColorText, 1, right - 8 - left);
    ly += 16;
    for (size_t i = 0; i < options.size(); i++, ly += 56)
    {
        bool picked = (int)i == selected;
        c.FillRoundRect(left - 8, ly - 7, right - left + 8, 46, 23, picked ? kColorGreen : kColorKey);
        if (picked)
            DrawBoldText(c, left + 16, ly, options[i], kColorWhite, 1, right - left - 32);
        else
            c.DrawText(left + 16, ly, options[i], kColorText, 1, right - left - 32);
    }
}

// Scrolls `scroll` (pixels) so that [top, top + height) is inside a view of `viewHeight`.
static int ScrollToShow(int top, int height, int scroll, int viewHeight)
{
    if (top < scroll)
        return top;
    if (top + height > scroll + viewHeight)
        return top + height - viewHeight;
    return scroll;
}

// A thin bar on the right edge when the content is taller than the view.
static void DrawScrollBar(Canvas& c, int contentHeight, int scroll)
{
    int viewHeight = kViewBottom - kListTop;
    if (contentHeight <= viewHeight)
        return;
    int x = c.Width() - kMarginX + 24;
    c.FillRoundRect(x, kListTop, 8, viewHeight, 4, 0x1AF3ECE0);
    int barHeight = viewHeight * viewHeight / contentHeight;
    barHeight = barHeight < 24 ? 24 : barHeight;
    int barTop = kListTop + (viewHeight - barHeight) * scroll / (contentHeight - viewHeight);
    c.FillRoundRect(x, barTop, 8, barHeight, 4, kColorGreen);
}

// ---------------------------------------------------------------------------
// On-screen keyboard

static const char* kKeyRows[3] = { "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789-'&" };
static const int kKeyColumns = 13;
static const char* kKeyActions[4] = { "Space", "Delete", "Clear", "Show results" };
static const int kKeyActionCount = 4;

// ---------------------------------------------------------------------------
// The app

enum ScreenId
{
    kScreenMenu,
    kScreenItems,
    kScreenSearchResults,
    kScreenProfiles,
};

enum Overlay
{
    kOverlayNone,
    kOverlayItemActions,
    kOverlayAddAllActions,
    kOverlayConfirmActivate,
    kOverlayMessage,
    kOverlayWorking,
    kOverlayKeyboard,
};

enum PendingWork
{
    kWorkNone,
    kWorkOpenItems,
    kWorkScanItems,
    kWorkInstall,
    kWorkInstallAndRemove,
    kWorkInstallAll,
    kWorkInstallAllAndRemove,
    kWorkIndexFolders,
    kWorkScanProfiles,
    kWorkActivate,
    kWorkScreenshot,
};

// Where the item screen was, to come back to it.
struct ItemPosition
{
    std::string folder;
    int selected;
    int scroll;
    bool toResults;     // B goes back to the search results instead
};

class App
{
public:
    App(Screen& screen) : m_screen(screen), m_screenId(kScreenMenu), m_overlay(kOverlayNone), m_work(kWorkNone),
        m_workShown(false), m_menuIndex(0), m_selected(0), m_scroll(0), m_folderIndexBuilt(false), m_keyRow(0),
        m_keyColumn(0), m_resultIndex(0), m_resultScroll(0), m_driveIndex(0), m_profileIndex(0), m_profileScroll(0),
        m_optionIndex(0), m_toastUntil(0), m_quit(false), m_dirty(true) {}


    bool Quit() const { return m_quit; }

    // True once after anything on screen changed.
    bool TakeDirty()
    {
        bool dirty = m_dirty;
        m_dirty = false;
        return dirty;
    }

    // Hides the notification once its time is up.
    void Tick()
    {
        if (m_toastUntil && NowMs() >= m_toastUntil)
        {
            m_toastUntil = 0;
            m_dirty = true;
        }
    }

    void HandleInput(u16 pressed)
    {
        if (!pressed || m_overlay == kOverlayWorking)
            return;
        if (pressed & kButtonBack)
        {
            m_quit = true;
            return;
        }
        if (m_overlay != kOverlayNone)
            HandleOverlay(pressed);
        else if (m_screenId == kScreenMenu)
            HandleMenu(pressed);
        else if (m_screenId == kScreenItems)
            HandleItems(pressed);
        else if (m_screenId == kScreenSearchResults)
            HandleResults(pressed);
        else
            HandleProfiles(pressed);
        m_dirty = true;
    }

    // Runs work queued by the input handlers, after "Working..." has been shown.
    void RunPendingWork()
    {
        if (m_overlay != kOverlayWorking || !m_workShown)
            return;
        PendingWork work = m_work;
        m_work = kWorkNone;
        m_overlay = kOverlayNone;
        switch (work)
        {
        case kWorkOpenItems:
            OpenItems();
            break;
        case kWorkScanItems:
            RescanFolder();
            break;
        case kWorkInstall:
        case kWorkInstallAndRemove:
            Install(work == kWorkInstallAndRemove);
            break;
        case kWorkInstallAll:
        case kWorkInstallAllAndRemove:
            InstallAll(work == kWorkInstallAllAndRemove);
            break;
        case kWorkIndexFolders:
            m_folderIndex = IndexItemFolders();
            m_folderIndexBuilt = true;
            OpenKeyboard();
            break;
        case kWorkScanProfiles:
            m_profiles = ScanProfiles();
            if (m_profileIndex >= (int)m_profiles.size())
                m_profileIndex = 0;
            break;
        case kWorkActivate:
            Activate();
            break;
        case kWorkScreenshot:
            SaveTakenScreenshot();
            break;
        case kWorkNone:
            break;
        }
        m_dirty = true;
    }

    void Draw()
    {
        Canvas& c = m_screen.canvas();
        if (m_screenId == kScreenMenu)
            DrawMenu(c);
        else if (m_screenId == kScreenItems)
            DrawItems(c);
        else if (m_screenId == kScreenSearchResults)
            DrawResults(c);
        else
            DrawProfiles(c);

        if (m_overlay == kOverlayItemActions && SelectedItem())
        {
            const Item& item = *SelectedItem();
            std::vector<std::string> lines(1, item.name);
            std::vector<std::string> options;
            if (DriveRows())
                options.push_back(DriveOption());
            options.push_back(item.installed ? "Unlock again" : "Unlock");
            options.push_back("Unlock and delete file");
            options.push_back("Cancel");
            std::string drives;
            for (size_t i = 0; i < item.installedOn.size(); i++)
                drives += (i ? ", " : "") + item.installedOn[i];
            lines.push_back(item.installed ? "Already unlocked for every profile (" + drives + ")"
                                           : "New - unlocks it for every profile");
            DrawDialog(c, "Avatar item", lines, options, m_optionIndex, &item.icon);
        }
        else if (m_overlay == kOverlayAddAllActions)
        {
            std::vector<std::string> lines(1, m_folder.empty() ? "Every item in AvatarItems," : "Every item in this folder,");
            lines.push_back("subfolders included.");
            std::vector<std::string> options;
            if (DriveRows())
                options.push_back(DriveOption());
            options.push_back("Unlock all");
            options.push_back("Unlock all and delete files");
            options.push_back("Cancel");
            DrawDialog(c, "Unlock all items", lines, options, m_optionIndex);
        }
        else if (m_overlay == kOverlayConfirmActivate && m_profileIndex < (int)m_profiles.size())
        {
            const ProfileEntry& p = m_profiles[m_profileIndex];
            std::vector<std::string> lines;
            char xuid[24];
            snprintf(xuid, sizeof xuid, "%016llX", (unsigned long long)kSharedOnlineXuid);
            lines.push_back("New online XUID for " + p.info.gamertag + ":");
            lines.push_back(std::string("  ") + xuid);
            lines.push_back("(the same for every activated profile)");
            lines.push_back("Re-signed with this console's key; a backup");
            lines.push_back("is saved to Backups first.");
            std::vector<std::string> options;
            options.push_back("Activate " + p.info.gamertag);
            options.push_back("Cancel");
            DrawDialog(c, "Activate offline profile", lines, options, m_optionIndex);
        }

        else if (m_overlay == kOverlayMessage)
            DrawDialog(c, m_messageTitle, m_messageLines, std::vector<std::string>(1, "OK"), 0);
        else if (m_overlay == kOverlayKeyboard)
            DrawKeyboard(c);
        else if (m_overlay == kOverlayWorking)
        {
            DrawDialog(c, "Working...", std::vector<std::string>(1, m_workText), std::vector<std::string>(), -1);
            m_workShown = true;
        }

        if (m_toastUntil && m_overlay == kOverlayNone)
            DrawToast(c);
    }

private:
    void StartWork(PendingWork work, const std::string& text)
    {
        m_work = work;
        m_workText = text;
        m_workShown = false;
        m_overlay = kOverlayWorking;
    }

    void ShowMessage(const std::string& title, const std::vector<std::string>& lines)
    {
        m_messageTitle = title;
        m_messageLines = lines;
        m_overlay = kOverlayMessage;
    }

    // A short notification at the bottom that goes away by itself.
    void ShowToast(const std::string& title, const std::string& text, const std::vector<u32>& icon)
    {
        m_toastTitle = title;
        m_toastText = text;
        m_toastIcon = icon;
        m_toastUntil = NowMs() + kToastMs;
    }

    // A bar over the info and hint lines at the bottom, so it hides nothing of the list.
    void DrawToast(Canvas& c)
    {
        int x = kMarginX - 16;
        int y = kViewBottom + 4;
        int w = c.Width() - 2 * x;
        int h = 684 - y;   // down to the bottom of the title-safe area
        DrawCard(c, x, y, w, h, h / 2, false, false, kColorGreen);
        int iconSize = h - 16;
        int iconX = x + 12;
        c.FillCircle(iconX + iconSize / 2, y + h / 2, iconSize / 2, kColorWhite);
        if (m_toastIcon.empty())
            DrawTick(c, iconX + iconSize / 2 - 18, y + h / 2 - 15, 2, kColorGreen);
        else
            c.DrawImageScaled(iconX + 4, y + 12, iconSize - 8, iconSize - 8, &m_toastIcon[0], kIconSize, kIconSize);
        int textX = iconX + iconSize + 24;
        int textY = y + (h - 32) / 2;
        DrawBoldText(c, textX, textY, m_toastTitle, kColorWhite);
        int nameX = textX + Canvas::TextWidth(m_toastTitle) + 32;
        c.DrawText(nameX, textY, m_toastText, kColorWhite, 1, x + w - 40 - nameX);
    }

    // --- start menu

    void HandleMenu(u16 pressed)
    {
        if (pressed & (kButtonLeft | kButtonUp))
            m_menuIndex = (m_menuIndex + kMenuCards - 1) % kMenuCards;
        if (pressed & (kButtonRight | kButtonDown))
            m_menuIndex = (m_menuIndex + 1) % kMenuCards;
        if (!(pressed & kButtonA))
            return;
        if (m_menuIndex == 0)
            StartWork(kWorkOpenItems, "Reading AvatarItems...");
        else if (m_menuIndex == 1)
        {
            m_screenId = kScreenProfiles;
            StartWork(kWorkScanProfiles, "Looking for profiles...");
        }
        else
            OpenAvatarEditor();
    }

    // The editor works on a signed-in profile's avatar. The system then ends
    // this app; if the app is still running, the system's answer is shown.
    void OpenAvatarEditor()
    {
        std::string gamertag;
        int user = FirstSignedInUser(&gamertag);
        if (user < 0)
        {
            ShowMessage("No profile signed in", std::vector<std::string>(1, "Please sign in to jump to the avatar editor."));
            return;
        }
        u32 result = LaunchAvatarEditor(user);
        char code[64];
        snprintf(code, sizeof code, "The system answered 0x%08X.", (unsigned)result);
        std::vector<std::string> lines(1, "Opening the avatar editor for " + gamertag + ".");
        lines.push_back(code);
        ShowMessage("Avatar Editor", lines);
    }

    // Decodes card `index`'s picture (menu_art.h) to ARGB, scaled to w x h if
    // the cards aren't the size it was made for.
    void LoadMenuArt(int index, int w, int h)
    {
        int sw = 0;
        int sh = 0;
        int channels = 0;
        unsigned char* rgba = stbi_load_from_memory(g_menuArt[index], (int)g_menuArtSize[index], &sw, &sh, &channels, 4);
        if (!rgba)
        {
            xsdk_log_write("menu: picture %d can't be decoded", index);
            return;
        }
        std::vector<u32> argb(sw * sh);
        for (int p = 0; p < sw * sh; p++)
            argb[p] = (u32)rgba[p * 4 + 3] << 24 | (u32)rgba[p * 4] << 16 | (u32)rgba[p * 4 + 1] << 8 | rgba[p * 4 + 2];
        stbi_image_free(rgba);
        if (sw == w && sh == h)
            m_menuArt[index].swap(argb);
        else
            Canvas::ScaleImage(&argb[0], sw, sh, w, h, &m_menuArt[index]);
    }

    void DrawMenu(Canvas& c)
    {
        DrawHeader(c, "");
        static const char* kOptions[kMenuCards] = { "Unlock Items", "Activate Offline Profile", "Avatar Editor" };
        static const char* kHelp[kMenuCards] =
        {
            "Unlock items in AvatarItems for every profile",
            "Get an offline profile ready for unlocked items",
            "Go straight to the dashboard's Avatar Editor",
        };
        // The card's colour, shown if its picture can't be decoded.
        static const u32 kHero[kMenuCards] = { 0xFF5E8F4C, 0xFF6E5C8A, 0xFF4F6F8F };
        const int gap = 30;
        const int w = (c.Width() - 2 * kMarginX + 32 - (kMenuCards - 1) * gap) / kMenuCards;
        const int h = 420;
        const int hero = 190;
        static bool s_artLoaded = false;
        if (!s_artLoaded)
        {
            for (int i = 0; i < kMenuCards; i++)
                LoadMenuArt(i, w, hero);
            s_artLoaded = true;
        }
        for (int i = 0; i < kMenuCards; i++)
        {
            int x = kMarginX - 16 + i * (w + gap);
            int y = DrawCard(c, x, 168, w, h, 28, i == m_menuIndex, true);
            if (m_menuArt[i].size() == (size_t)(w * hero))
                c.DrawImage(x, y, w, hero, &m_menuArt[i][0]);
            else
            {
                c.FillRoundRect(x, y, w, hero, 28, kHero[i]);
                c.FillRect(x, y + hero - 28, w, 28, kHero[i]);
            }
            int ty = y + hero + 20;
            std::vector<std::string> title = WrapText(kOptions[i], w - 56);
            for (size_t l = 0; l < title.size(); l++, ty += 34)
                DrawBoldText(c, x + 28, ty, title[l], kColorText);
            ty += 12;
            std::vector<std::string> help = WrapText(kHelp[i], w - 56);
            for (size_t l = 0; l < help.size(); l++, ty += 34)
                c.DrawText(x + 28, ty, help[l], kColorDim);
        }
        DrawFooter(c, "A select   BACK quit");
    }

    // --- items: a list ("Unlock all", then the folders) above a grid of item tiles

    int ListRows() const
    {
        bool any = !m_listing.folders.empty() || !m_listing.items.empty();
        return (any ? 1 : 0) + (int)m_listing.folders.size();
    }

    int Tiles() const { return (int)m_listing.items.size(); }

    // Position of element `index` in the scrolled content, from its top.
    void ElementRect(int index, int* x, int* y, int* w, int* h) const
    {
        int rows = ListRows();
        if (index < rows)
        {
            *x = kMarginX - 16;
            *y = index * kRowHeight;
            *w = m_screen.canvas().Width() - 2 * kMarginX + 32;
            *h = kRowHeight;
            return;
        }
        int tile = index - rows;
        *x = (m_screen.canvas().Width() - kTileColumns * kTileWidth) / 2 + (tile % kTileColumns) * kTileWidth;
        *y = rows * kRowHeight + (rows ? 12 : 0) + (tile / kTileColumns) * kTileHeight;
        *w = kTileWidth;
        *h = kTileHeight;
    }

    int ContentHeight() const
    {
        int total = ListRows() + Tiles();
        if (total == 0)
            return 0;
        int x, y, w, h;
        ElementRect(total - 1, &x, &y, &w, &h);
        return y + h;
    }

    const Item* SelectedItem() const
    {
        int tile = m_selected - ListRows();
        if (tile < 0 || tile >= Tiles())
            return NULL;
        return &m_listing.items[tile];
    }

    void ScrollToSelected()
    {
        if (ListRows() + Tiles() == 0)
        {
            m_scroll = 0;
            return;
        }
        int x, y, w, h;
        ElementRect(m_selected, &x, &y, &w, &h);
        // Keep the list's top in view while the first rows are selected.
        if (m_selected == 0)
            y = 0;
        m_scroll = ScrollToShow(y, h, m_scroll, kViewBottom - kListTop);
    }

    // Items are unlocked for every profile: the shared XUID (activated offline
    // profiles) and each Xbox Live profile's own. Installed items get the XUIDs
    // of Live profiles that turned up since added here.
    void OpenItems()
    {
        // The drive picked before stays picked while it's there; the app's own
        // drive comes first otherwise.
        std::string picked = m_drives.empty() ? AppDrive().name : m_drives[m_driveIndex].name;
        m_drives = AvailableDrives();
        m_driveIndex = 0;
        for (size_t i = 0; i < m_drives.size(); i++)
            if (m_drives[i].name == picked)
                m_driveIndex = (int)i;
        m_licensees = ItemLicensees();
        InstallAllResult updated = UpdateInstalledItems(m_licensees, m_drives);
        xsdk_log_write("items: %d licensees, %d installed items updated, %d failed", (int)m_licensees.size(),
                       updated.installed, updated.failed);
        m_screenId = kScreenItems;
        m_folder.clear();
        m_positions.clear();
        m_selected = 0;
        m_scroll = 0;
        m_folderIndexBuilt = false;   // the folders may have changed since
        RescanFolder();
        char summary[96];
        if (updated.failed > 0)
        {
            snprintf(summary, sizeof summary, "%d unlocked, %d failed.", updated.installed, updated.failed);
            std::vector<std::string> lines(1, "Unlocking items again for new profiles:");
            lines.push_back(summary);
            for (size_t i = 0; i < updated.errors.size(); i++)
                lines.push_back(updated.errors[i]);
            ShowMessage("Partly done", lines);
        }
        else if (updated.installed > 0)
        {
            snprintf(summary, sizeof summary, "%d item%s now also unlocked for new profiles", updated.installed,
                     updated.installed == 1 ? "" : "s");
            ShowToast("Unlocked for new profiles", summary, std::vector<u32>());
        }
    }

    // Loads one tile's details (full check, preview, license) and scales its
    // preview to tile size, once. True if there was anything to do.
    bool LoadTile(int tile)
    {
        if (tile < 0 || tile >= Tiles())
            return false;
        Item& item = m_listing.items[tile];
        bool work = !item.detailsLoaded;
        LoadItemDetails(&item, m_licensees, m_drives);
        if (m_tileImages[tile].empty() && !item.icon.empty())
        {
            Canvas::ScaleImage(&item.icon[0], kIconSize, kIconSize, kTileImage, kTileImage, &m_tileImages[tile]);
            work = true;
        }
        return work;
    }

public:
    // Keeps the screen as it is now (the last frame drawn, without the
    // "Working..." that follows) and saves it as a PNG next to the app.
    void TakeScreenshot()
    {
        if (m_overlay == kOverlayWorking)
            return;
        m_screen.canvas().SaveTo(&m_screenshot);
        StartWork(kWorkScreenshot, "Saving screenshot...");
        m_dirty = true;
    }

    // Loads the details of the tiles on screen and the row below, for at most
    // about 30 ms per frame so input stays responsive.
    void LoadVisibleDetails()
    {
        if (m_screenId != kScreenItems || m_overlay == kOverlayWorking)
            return;
        int rows = ListRows();
        int viewTop = m_scroll;
        int viewBottom = m_scroll + (kViewBottom - kListTop) + kTileHeight;
        u32 start = NowMs();
        for (int tile = 0; tile < Tiles(); tile++)
        {
            int x, y, w, h;
            ElementRect(rows + tile, &x, &y, &w, &h);
            if (y + h < viewTop || y > viewBottom)
                continue;
            if (LoadTile(tile))
                m_dirty = true;
            if (NowMs() - start > 30)
                break;
        }
    }

private:
    void RescanFolder()
    {
        // Headers only; previews come with LoadVisibleDetails.
        m_listing = ScanItemFolder(m_folder, m_drives);
        m_tileImages.assign(m_listing.items.size(), std::vector<u32>());
        int total = ListRows() + Tiles();
        if (m_selected >= total)
            m_selected = total > 0 ? total - 1 : 0;
        ScrollToSelected();
    }

    void OpenFolder(const std::string& folder, bool fromResults)
    {
        ItemPosition position = { m_folder, m_selected, m_scroll, fromResults };
        if (fromResults)
            m_positions.clear();
        m_positions.push_back(position);
        m_folder = folder;
        m_selected = 0;
        m_scroll = 0;
        m_screenId = kScreenItems;
        StartWork(kWorkScanItems, "Reading " + FolderName(folder) + "...");
    }

    void MoveInItems(u16 pressed)
    {
        int rows = ListRows();
        int tiles = Tiles();
        int total = rows + tiles;
        if (total == 0)
            return;
        bool inGrid = m_selected >= rows;
        int tile = m_selected - rows;
        if (pressed & kButtonUp)
        {
            if (!inGrid)
                m_selected = m_selected > 0 ? m_selected - 1 : 0;
            else if (tile >= kTileColumns)
                m_selected -= kTileColumns;
            else if (rows > 0)
                m_selected = rows - 1;
        }
        if (pressed & kButtonDown)
        {
            if (!inGrid)
                m_selected = m_selected < total - 1 ? m_selected + 1 : m_selected;
            else if (tile + kTileColumns < tiles)
                m_selected += kTileColumns;
            else if (tile / kTileColumns < (tiles - 1) / kTileColumns)
                m_selected = total - 1;   // the last row is shorter
        }
        if ((pressed & kButtonLeft) && inGrid && tile > 0)
            m_selected--;
        if ((pressed & kButtonRight) && inGrid && tile < tiles - 1)
            m_selected++;
        int page = inGrid ? 2 * kTileColumns : 5;
        if (pressed & kButtonLB)
            m_selected = m_selected - page > 0 ? m_selected - page : 0;
        if (pressed & kButtonRB)
            m_selected = m_selected + page < total ? m_selected + page : total - 1;
        ScrollToSelected();
    }

    void HandleItems(u16 pressed)
    {
        if (pressed & kButtonB)
        {
            if (m_positions.empty())
            {
                m_screenId = kScreenMenu;
                m_toastUntil = 0;
                return;
            }
            ItemPosition back = m_positions.back();
            m_positions.pop_back();
            if (back.toResults)
            {
                m_screenId = kScreenSearchResults;
                return;
            }
            m_folder = back.folder;
            m_selected = back.selected;
            m_scroll = back.scroll;
            StartWork(kWorkScanItems, "Reading the folder...");
            return;
        }
        if (pressed & kButtonY)
        {
            StartSearch();
            return;
        }
        MoveInItems(pressed);
        if (!(pressed & kButtonA) || ListRows() + Tiles() == 0)
            return;

        int rows = ListRows();
        if (m_selected == 0)
        {
            m_optionIndex = DriveRows();   // on "Unlock all", below the drive
            m_overlay = kOverlayAddAllActions;
        }
        else if (m_selected < rows)
        {
            const std::string& name = m_listing.folders[m_selected - 1];
            OpenFolder(m_folder.empty() ? name : m_folder + "\\" + name, false);
        }
        else
        {
            LoadTile(m_selected - rows);   // in case it wasn't on screen long enough
            if (!SelectedItem()->error.empty())
                ShowMessage(SelectedItem()->name,
                            std::vector<std::string>(1, "Can't be unlocked: " + SelectedItem()->error));
            else
            {
                m_optionIndex = DriveRows();
                m_overlay = kOverlayItemActions;
            }
        }
    }

    // A product card: the preview on a muted spotlight, a tag, the name.
    void DrawTile(Canvas& c, int x, int y, const Item& item, bool selected, int index)
    {
        int cardX = x + 10;
        int cardW = kTileWidth - 20;
        int top = DrawCard(c, cardX, y + 6, cardW, kTileHeight - 12, 18, selected, true,
                           selected ? kColorCardPicked : kColorCard);
        int imageX = x + (kTileWidth - kTileImage) / 2;
        int imageY = top + 6;
        if (item.error.empty())
            c.FillCircle(imageX + kTileImage / 2, imageY + kTileImage / 2, kTileImage / 2 - 6, kPastels[index % 5]);
        DrawItemImage(c, imageX, imageY, kTileImage, item, m_tileImages[index]);
        if (item.error.empty())
            DrawTag(c, cardX + 8, top + 8, item.installed);
        u32 color = !item.error.empty() ? kColorBad : kColorText;
        DrawTextCenteredAt(c, x + kTileWidth / 2, imageY + kTileImage + 2, item.name, color, cardW - 20);
    }

    // A list row's card, with a ">" when it opens something.
    void DrawRowCard(Canvas& c, int x, int y, int w, bool selected, bool chevron, u32 fill = kColorCard)
    {
        DrawCard(c, x, y - 4, w, kRowHeight - 8, 18, selected, false, selected && fill == kColorCard ? kColorCardPicked : fill);
        if (chevron)
            DrawBoldText(c, x + w - 44, y + 14, ">", selected ? kColorGreen : kColorDim);
    }

    void DrawItems(Canvas& c)
    {
        DrawHeader(c, m_folder.empty() ? "Unlock Items" : m_folder);
        int rows = ListRows();
        int total = rows + Tiles();
        if (total == 0 && m_overlay != kOverlayWorking)
        {
            if (m_folder.empty())
            {
                c.DrawText(kMarginX, kListTop, "No avatar items found.", kColorText);
                c.DrawText(kMarginX, kListTop + 48, "Copy item packages (or folders of them) to the", kColorDim);
                c.DrawText(kMarginX, kListTop + 88, "AvatarItems folder next to AvatarThriftStore.xex.", kColorDim);
            }
            else
                c.DrawText(kMarginX, kListTop, "This folder is empty.", kColorText);
        }

        c.SetClip(0, kListTop - 12, c.Width(), kViewBottom - kListTop + 12);
        int textX = kMarginX + 64 + 24;
        int textW = c.Width() - kMarginX - 72 - textX;
        for (int i = 0; i < total; i++)
        {
            int x, y, w, h;
            ElementRect(i, &x, &y, &w, &h);
            y += kListTop - m_scroll;
            if (y + h < kListTop - 12 || y > kViewBottom)
                continue;
            if (i >= rows)
            {
                DrawTile(c, x, y, m_listing.items[i - rows], i == m_selected, i - rows);
                continue;
            }
            if (i == 0)
            {
                DrawRowCard(c, x, y, w, i == m_selected, false, kColorGreen);
                DrawAddAllIcon(c, kMarginX, y);
                DrawBoldText(c, textX, y - 2, m_folder.empty() ? "Unlock all items" : "Unlock all items in this folder",
                             kColorWhite, 1, textW);
                c.DrawText(textX, y + 30, "Subfolders included", 0xFFE3EDDC, 1, textW);
            }
            else
            {
                const std::string& name = m_listing.folders[i - 1];
                DrawRowCard(c, x, y, w, i == m_selected, true);
                DrawFolderIcon(c, kMarginX, y, name);
                c.DrawText(textX, y + 14, name, kColorText, 1, textW);
            }
        }
        c.ResetClip();
        DrawScrollBar(c, ContentHeight(), m_scroll);

        // The selected item's full name, which a tile can't fit, and its state
        // when it is unlocked or can't be unlocked (new ones just show the name).
        if (const Item* item = SelectedItem())
        {
            int nameEnd = c.Width() - kMarginX;
            if (!item->error.empty() || item->installed)
            {
                std::string state = !item->error.empty() ? item->error : "Unlocked";
                int stateW = Canvas::TextWidth(state) + 40;
                int maxW = (c.Width() - 2 * kMarginX) / 2;
                stateW = stateW < maxW ? stateW : maxW;
                int stateX = c.Width() - kMarginX - stateW;
                c.FillRoundRect(stateX, kInfoY - 4, stateW, 40, 20, !item->error.empty() ? kColorBad : kColorGreen);
                c.DrawText(stateX + 20, kInfoY, state, kColorWhite, 1, stateW - 40);
                nameEnd = stateX - 32;
            }
            DrawBoldText(c, kMarginX, kInfoY, item->name, kColorText, 1, nameEnd - kMarginX);
        }

        bool onFolder = m_selected > 0 && m_selected < ListRows();
        DrawFooter(c, std::string(onFolder ? "A open" : "A unlock") + "   B " + (m_positions.empty() ? "back" : "up") +
                          "   Y search   LB/RB page");
    }

    // The unlock dialogs start with a row that picks the drive, when there's
    // more than one (the hard disk and a USB stick).
    int DriveRows() const { return m_drives.size() > 1 ? 1 : 0; }

    std::string DriveOption() const { return "Unlock on:  < " + m_drives[m_driveIndex].name + " >"; }

    // "Unlocked", or "Unlocked on USB 1" when there was a choice.
    std::string UnlockedTitle(const std::string& suffix) const
    {
        return (DriveRows() ? "Unlocked on " + m_drives[m_driveIndex].name : std::string("Unlocked")) + suffix;
    }

    void Install(bool removeSource)
    {
        const Item* item = SelectedItem();
        if (!item)
            return;
        int tile = m_selected - ListRows();
        std::string name = item->name;
        std::vector<u32> icon = item->icon;
        std::string error;
        const Drive& drive = m_drives[m_driveIndex];
        bool installed = InstallItemFile(item->path, removeSource, m_licensees, drive, &error);
        if (!installed)
        {
            if (removeSource)
                RescanFolder();   // it may be installed, with only the delete failed
            ShowMessage("Not unlocked", std::vector<std::string>(1, name + ": " + error));
            return;
        }
        // Only this tile changed; a large folder isn't read again for it.
        if (removeSource)
        {
            m_listing.items.erase(m_listing.items.begin() + tile);
            m_tileImages.erase(m_tileImages.begin() + tile);
            int total = ListRows() + Tiles();
            if (m_selected >= total)
                m_selected = total > 0 ? total - 1 : 0;
            ScrollToSelected();
        }
        else
        {
            Item& installedItem = m_listing.items[tile];
            installedItem.installed = true;
            if (std::find(installedItem.installedOn.begin(), installedItem.installedOn.end(), drive.name) ==
                installedItem.installedOn.end())
                installedItem.installedOn.push_back(drive.name);
        }
        ShowToast(UnlockedTitle(removeSource ? ", file removed" : ""), name, icon);
    }

    void InstallAll(bool removeSource)
    {
        InstallAllResult result = InstallAllItems(m_folder, removeSource, m_licensees, m_drives[m_driveIndex]);
        RescanFolder();
        char summary[96];
        if (result.failed == 0)
        {
            snprintf(summary, sizeof summary, "%d item%s", result.installed, result.installed == 1 ? "" : "s");
            ShowToast(UnlockedTitle(removeSource ? ", files removed" : ""), summary, std::vector<u32>());
            return;
        }
        snprintf(summary, sizeof summary, "%d unlocked, %d failed.", result.installed, result.failed);
        std::vector<std::string> lines(1, summary);
        for (size_t i = 0; i < result.errors.size(); i++)
            lines.push_back(result.errors[i]);
        if (result.failed > (int)result.errors.size())
            lines.push_back("...");
        ShowMessage("Partly done", lines);
    }

    // --- search

    void StartSearch()
    {
        if (m_screenId == kScreenItems)
        {
            ItemPosition origin = { m_folder, m_selected, m_scroll, false };
            m_searchOrigin = origin;
            m_searchOriginPositions = m_positions;
        }
        if (m_folderIndexBuilt)
            OpenKeyboard();
        else
            StartWork(kWorkIndexFolders, "Indexing folders...");
    }

    void OpenKeyboard()
    {
        m_keyRow = 0;
        m_keyColumn = 0;
        m_matches = SearchFolders(m_folderIndex, m_query);
        m_overlay = kOverlayKeyboard;
    }

    void SetQuery(const std::string& query)
    {
        m_query = query;
        m_matches = SearchFolders(m_folderIndex, m_query);
    }

    void ShowResults()
    {
        m_overlay = kOverlayNone;
        m_screenId = kScreenSearchResults;
        m_resultIndex = 0;
        m_resultScroll = 0;
    }

    void HandleKeyboard(u16 pressed)
    {
        bool actionRow = m_keyRow == 3;
        if (pressed & kButtonUp)
        {
            if (actionRow)
                m_keyColumn = m_keyColumn * kKeyColumns / kKeyActionCount + 1;
            m_keyRow = m_keyRow > 0 ? m_keyRow - 1 : 3;
            if (m_keyRow == 3)
                m_keyColumn = m_keyColumn * kKeyActionCount / kKeyColumns;
        }
        else if (pressed & kButtonDown)
        {
            if (actionRow)
                m_keyColumn = m_keyColumn * kKeyColumns / kKeyActionCount + 1;
            m_keyRow = (m_keyRow + 1) % 4;
            if (m_keyRow == 3)
                m_keyColumn = m_keyColumn * kKeyActionCount / kKeyColumns;
        }
        int columns = m_keyRow == 3 ? kKeyActionCount : kKeyColumns;
        if (pressed & kButtonLeft)
            m_keyColumn = (m_keyColumn + columns - 1) % columns;
        if (pressed & kButtonRight)
            m_keyColumn = (m_keyColumn + 1) % columns;

        if (pressed & kButtonB)
        {
            m_overlay = kOverlayNone;
            return;
        }
        if (pressed & kButtonX)
            SetQuery(m_query.empty() ? m_query : m_query.substr(0, m_query.size() - 1));
        if ((pressed & kButtonY) && m_query.size() < kMaxQuery)
            SetQuery(m_query + " ");
        if ((pressed & kButtonStart) && !m_matches.empty())
        {
            ShowResults();
            return;
        }
        if (!(pressed & kButtonA))
            return;
        if (m_keyRow < 3)
        {
            if (m_query.size() < kMaxQuery)
                SetQuery(m_query + (char)tolower((unsigned char)kKeyRows[m_keyRow][m_keyColumn]));
            return;
        }
        switch (m_keyColumn)
        {
        case 0:
            if (m_query.size() < kMaxQuery)
                SetQuery(m_query + " ");
            break;
        case 1:
            SetQuery(m_query.empty() ? m_query : m_query.substr(0, m_query.size() - 1));
            break;
        case 2:
            SetQuery("");
            break;
        default:
            if (!m_matches.empty())
                ShowResults();
            break;
        }
    }

    void DrawKey(Canvas& c, int x, int y, int w, const std::string& label, bool selected)
    {
        c.FillRoundRect(x, y + 3, w, 52, 14, 0x40000000);
        c.FillRoundRect(x, y, w, 52, 14, selected ? kColorGreen : kColorKey);
        if (selected)
        {
            int width = Canvas::TextWidth(label) < w - 8 ? Canvas::TextWidth(label) : w - 8;
            DrawBoldText(c, x + w / 2 - width / 2, y + 10, label, kColorWhite, 1, w - 8);
        }
        else
            DrawTextCenteredAt(c, x + w / 2, y + 10, label, kColorText, w - 8);
    }

    void DrawKeyboard(Canvas& c)
    {
        int w = 1000;
        int h = 500;
        int x = (c.Width() - w) / 2;
        int y = (c.Height() - h) / 2 + 8;
        c.FillRect(0, 0, c.Width(), c.Height(), kColorShade);
        DrawCard(c, x, y, w, h, 24, false, false);
        c.FillRoundRect(x, y, w, 64, 24, kColorGreen);
        c.FillRect(x, y + 40, w, 24, kColorGreen);
        DrawBoldText(c, x + 32, y + 16, "Search folders", kColorWhite);
        y += 12;

        // The query, with a cursor.
        c.FillRoundRect(x + 32, y + 64, w - 64, 52, 26, kColorKey);
        c.DrawText(x + 56, y + 74, m_query + "_", kColorText, 1, w - 112);

        // How many folders match, and the first few.
        char count[64];
        if (m_query.empty())
            snprintf(count, sizeof count, "%d folders", (int)m_folderIndex.size());
        else
            snprintf(count, sizeof count, "%d found", (int)m_matches.size());
        c.DrawText(x + 32, y + 128, count, m_matches.empty() && !m_query.empty() ? kColorWarn : kColorDim);
        std::string preview;
        for (size_t i = 0; i < m_matches.size() && i < 3; i++)
            preview += (i ? ", " : "") + FolderName(m_matches[i]);
        int previewX = x + 48 + Canvas::TextWidth(count);
        c.DrawText(previewX, y + 128, preview, kColorText, 1, x + w - 32 - previewX);

        int keyW = 68;
        int keysX = x + (w - kKeyColumns * keyW) / 2;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < kKeyColumns; col++)
                DrawKey(c, keysX + col * keyW + 2, y + 184 + row * 60, keyW - 4, std::string(1, kKeyRows[row][col]),
                        m_keyRow == row && m_keyColumn == col);
        int actionW = kKeyColumns * keyW / kKeyActionCount;
        for (int i = 0; i < kKeyActionCount; i++)
            DrawKey(c, keysX + i * actionW + 2, y + 184 + 3 * 60, actionW - 4, kKeyActions[i],
                    m_keyRow == 3 && m_keyColumn == i);

        DrawHints(c, x + 32, y + h - 72, "A type   X delete   Y space   START results   B close");
    }

    void HandleResults(u16 pressed)
    {
        if (pressed & kButtonB)
        {
            m_screenId = kScreenItems;
            m_folder = m_searchOrigin.folder;
            m_selected = m_searchOrigin.selected;
            m_scroll = m_searchOrigin.scroll;
            m_positions = m_searchOriginPositions;
            StartWork(kWorkScanItems, "Reading the folder...");
            return;
        }
        if (pressed & kButtonY)
        {
            StartSearch();
            return;
        }
        int count = (int)m_matches.size();
        if (count == 0)
            return;
        int visible = (kViewBottom - kListTop) / kRowHeight;
        if (pressed & kButtonUp)
            m_resultIndex = (m_resultIndex + count - 1) % count;
        if (pressed & kButtonDown)
            m_resultIndex = (m_resultIndex + 1) % count;
        if (pressed & kButtonLB)
            m_resultIndex = m_resultIndex - visible > 0 ? m_resultIndex - visible : 0;
        if (pressed & kButtonRB)
            m_resultIndex = m_resultIndex + visible < count ? m_resultIndex + visible : count - 1;
        m_resultScroll = ScrollToShow(m_resultIndex * kRowHeight, kRowHeight, m_resultScroll, visible * kRowHeight);
        if (pressed & kButtonA)
            OpenFolder(m_matches[m_resultIndex], true);
    }

    void DrawResults(Canvas& c)
    {
        DrawHeader(c, "Search: " + m_query);
        int textX = kMarginX + 64 + 24;
        int textW = c.Width() - kMarginX - 72 - textX;
        c.SetClip(0, kListTop - 12, c.Width(), kViewBottom - kListTop + 12);
        for (int i = 0; i < (int)m_matches.size(); i++)
        {
            int y = kListTop + i * kRowHeight - m_resultScroll;
            if (y + kRowHeight < kListTop - 12 || y > kViewBottom)
                continue;
            const std::string& path = m_matches[i];
            DrawRowCard(c, kMarginX - 16, y, c.Width() - 2 * kMarginX + 32, i == m_resultIndex, true);
            DrawFolderIcon(c, kMarginX, y, FolderName(path));
            size_t slash = path.rfind('\\');
            DrawBoldText(c, textX, y - 2, FolderName(path), kColorText, 1, textW);
            c.DrawText(textX, y + 32, "in AvatarItems" + (slash == std::string::npos ? "" : "\\" + path.substr(0, slash)),
                       kColorDim, 1, textW);
        }
        c.ResetClip();
        DrawScrollBar(c, (int)m_matches.size() * kRowHeight, m_resultScroll);
        char count[64];
        snprintf(count, sizeof count, "%d folders found", (int)m_matches.size());
        c.DrawText(c.Width() - kMarginX - Canvas::TextWidth(count), kInfoY, count, kColorDim);
        DrawFooter(c, "A open   B back   Y search again   LB/RB page");
    }

    // --- profiles

    static std::string StateText(const ProfileEntry& p, u32* color)
    {
        if (!p.error.empty())
        {
            *color = kColorBad;
            return "Unreadable: " + p.error;
        }
        if (p.info.kind == kProfileActivated)
        {
            *color = kColorGood;
            return "Activated - ready for items";
        }
        if (p.info.kind == kProfileLive)
        {
            *color = kColorDim;
            if (p.info.onlineXuid)
            {
                *color = kColorGood;
                return "Xbox Live - ready for items";
            }
            return "Xbox Live profile without an online XUID";
        }
        if (p.signedIn)
        {
            *color = kColorWarn;
            return "Offline - signed in, sign out to activate";
        }
        *color = kColorText;
        return "Offline - press A to activate";
    }

    void HandleProfiles(u16 pressed)
    {
        if (pressed & kButtonB)
        {
            m_screenId = kScreenMenu;
            return;
        }
        int count = (int)m_profiles.size();
        if (count == 0)
            return;
        int visible = (kViewBottom - kListTop) / kRowHeight;
        if (pressed & kButtonUp)
            m_profileIndex = (m_profileIndex + count - 1) % count;
        if (pressed & kButtonDown)
            m_profileIndex = (m_profileIndex + 1) % count;
        m_profileScroll = ScrollToShow(m_profileIndex * kRowHeight, kRowHeight, m_profileScroll, visible * kRowHeight);
        if (!(pressed & kButtonA))
            return;
        const ProfileEntry& p = m_profiles[m_profileIndex];
        u32 color;
        if (p.error.empty() && p.info.kind == kProfileOffline && !p.signedIn)
        {
            m_optionIndex = 0;
            m_overlay = kOverlayConfirmActivate;
        }
        else
            ShowMessage(p.info.gamertag, std::vector<std::string>(1, StateText(p, &color)));
    }

    void DrawProfiles(Canvas& c)
    {
        DrawHeader(c, "Profiles");
        if (m_profiles.empty() && m_overlay != kOverlayWorking)
            c.DrawText(kMarginX, kListTop, "No profiles found on the hard disk or USB storage.", kColorText);
        c.SetClip(0, kListTop - 12, c.Width(), kViewBottom - kListTop + 12);
        int textX = kMarginX + 64 + 24;
        for (int i = 0; i < (int)m_profiles.size(); i++)
        {
            int y = kListTop + i * kRowHeight - m_profileScroll;
            if (y + kRowHeight < kListTop - 12 || y > kViewBottom)
                continue;
            const ProfileEntry& p = m_profiles[i];
            DrawRowCard(c, kMarginX - 16, y, c.Width() - 2 * kMarginX + 32, i == m_profileIndex, false);
            // A round "gamerpic" with the gamertag's first letter.
            c.FillCircle(kMarginX + 32, y + 31, 30, kBrights[NameHash(p.info.gamertag) % 5]);
            std::string initial(1, p.info.gamertag.empty() ? '?' : (char)toupper((unsigned char)p.info.gamertag[0]));
            DrawBoldText(c, kMarginX + 24, y + 15, initial, kColorWhite);
            int driveW = Canvas::TextWidth(p.drive) + 32;
            int driveX = c.Width() - kMarginX - driveW;
            c.FillRoundRect(driveX, y + 12, driveW, 38, 19, kColorKey);
            c.DrawText(driveX + 16, y + 15, p.drive, kColorDim);
            DrawBoldText(c, textX, y - 2, p.info.gamertag, kColorText, 1, driveX - 24 - textX);
            // Its XUID next to the gamertag: the online one once it has one,
            // else the offline one the profile is named after.
            if (p.error.empty())
            {
                char xuid[48];
                if (p.info.onlineXuid)
                    snprintf(xuid, sizeof xuid, "XUID %016llX", (unsigned long long)p.info.onlineXuid);
                else
                    snprintf(xuid, sizeof xuid, "offline XUID %016llX", (unsigned long long)p.info.offlineXuid);
                int xuidX = textX + Canvas::TextWidth(p.info.gamertag) + 32;
                c.DrawText(xuidX, y - 2, xuid, kColorDim, 1, driveX - 24 - xuidX);
            }
            u32 color;
            std::string state = StateText(p, &color);
            c.DrawText(textX, y + 30, state, color, 1, driveX - 24 - textX);
        }
        c.ResetClip();
        DrawFooter(c, "A choose   B back");
    }

    void Activate()
    {
        ProfileEntry& p = m_profiles[m_profileIndex];
        std::string backup;
        std::string error;
        if (!ActivateProfile(p, &backup, &error))
        {
            xsdk_log_write("activate %s: %s", p.info.gamertag.c_str(), error.c_str());
            ShowMessage("Not activated", std::vector<std::string>(1, p.info.gamertag + ": " + error));
            return;
        }
        xsdk_log_write("activated %s, backup %s", p.info.gamertag.c_str(), backup.c_str());
        p.info.kind = kProfileActivated;
        std::vector<std::string> lines(1, p.info.gamertag + " is activated.");
        lines.push_back("Backup: " + backup);
        ShowMessage("Done", lines);
    }

    void SaveTakenScreenshot()
    {
        const Canvas& c = m_screen.canvas();
        std::string path;
        bool saved = SaveScreenshot(m_screenshot, c.Width(), c.Height(), &path);
        m_screenshot.clear();
        xsdk_log_write("screenshot: %s %s", saved ? "saved" : "failed", path.c_str());
        if (!saved)
        {
            ShowMessage("Screenshot not saved", std::vector<std::string>(1, "It couldn't be written next to the app."));
            return;
        }
        size_t slash = path.rfind('\\');
        ShowToast("Screenshot saved", slash == std::string::npos ? path : path.substr(slash + 1), std::vector<u32>());
    }

    // --- overlays

    void HandleOverlay(u16 pressed)
    {
        if (m_overlay == kOverlayKeyboard)
        {
            HandleKeyboard(pressed);
            return;
        }
        if (m_overlay == kOverlayMessage)
        {
            if (pressed & (kButtonA | kButtonB))
                m_overlay = kOverlayNone;
            return;
        }
        bool installing = m_overlay == kOverlayItemActions || m_overlay == kOverlayAddAllActions;
        int driveRows = installing ? DriveRows() : 0;
        int options = m_overlay == kOverlayConfirmActivate ? 2 : 3 + driveRows;
        if (pressed & kButtonUp)
            m_optionIndex = (m_optionIndex + options - 1) % options;
        if (pressed & kButtonDown)
            m_optionIndex = (m_optionIndex + 1) % options;
        if (pressed & kButtonB)
        {
            m_overlay = kOverlayNone;
            return;
        }
        // The drive row: left/right, or A, picks the next drive.
        if (driveRows && m_optionIndex == 0)
        {
            int count = (int)m_drives.size();
            if (pressed & kButtonLeft)
                m_driveIndex = (m_driveIndex + count - 1) % count;
            if (pressed & (kButtonRight | kButtonA))
                m_driveIndex = (m_driveIndex + 1) % count;
            return;
        }
        if (!(pressed & kButtonA))
            return;
        int action = m_optionIndex - driveRows;
        if (m_optionIndex == options - 1)   // Cancel
            m_overlay = kOverlayNone;
        else if (m_overlay == kOverlayItemActions && SelectedItem())
            StartWork(action == 0 ? kWorkInstall : kWorkInstallAndRemove,
                      "Unlocking " + SelectedItem()->name + " on " + m_drives[m_driveIndex].name);
        else if (m_overlay == kOverlayAddAllActions)
            StartWork(action == 0 ? kWorkInstallAll : kWorkInstallAllAndRemove,
                      "Unlocking all items on " + m_drives[m_driveIndex].name + "...");

        else
            StartWork(kWorkActivate, "Activating " + m_profiles[m_profileIndex].info.gamertag);
    }

    Screen& m_screen;
    ScreenId m_screenId;
    Overlay m_overlay;
    PendingWork m_work;
    std::string m_workText;
    bool m_workShown;
    int m_menuIndex;
    std::vector<u32> m_menuArt[kMenuCards];     // the cards' pictures, ARGB at card size

    // Item screen
    std::string m_folder;                       // below AvatarItems, "" at the top
    std::vector<ItemPosition> m_positions;      // where B goes back to
    ItemListing m_listing;
    std::vector<std::vector<u32> > m_tileImages; // the items' previews at tile size
    int m_selected;                             // list rows first, then tiles
    int m_scroll;                               // pixels

    // Search
    std::vector<std::string> m_folderIndex;
    bool m_folderIndexBuilt;
    std::string m_query;
    std::vector<std::string> m_matches;
    int m_keyRow;
    int m_keyColumn;
    int m_resultIndex;
    int m_resultScroll;
    ItemPosition m_searchOrigin;
    std::vector<ItemPosition> m_searchOriginPositions;

    std::vector<Drive> m_drives;                // where items can be installed (and are looked for)
    int m_driveIndex;                           // the one picked in the dialogs

    // Profiles
    std::vector<u64> m_licensees;               // XUIDs items get unlocked for
    std::vector<ProfileEntry> m_profiles;
    int m_profileIndex;
    int m_profileScroll;

    int m_optionIndex;
    std::string m_messageTitle;
    std::vector<std::string> m_messageLines;
    std::string m_toastTitle;
    std::string m_toastText;
    std::vector<u32> m_toastIcon;
    std::vector<u32> m_screenshot;              // the screen to save, taken when asked
    u32 m_toastUntil;
    bool m_quit;
    bool m_dirty;
};

int main()
{
    Screen screen;
    int status = 0;
    if (!screen.Open(&status))
    {
        // Returning ends the title: xsdk closes the GPU and goes back to the dashboard.
        xsdk_log_write("AvatarThriftStore: graphics init failed: 0x%08X", (unsigned)status);
        return 1;
    }

    App app(screen);
    Pad pad;
    while (!app.Quit())
    {
        app.HandleInput(pad.Poll());
        if (pad.ScreenshotPressed())
            app.TakeScreenshot();
        app.RunPendingWork();
        app.LoadVisibleDetails();
        app.Tick();
        // Redraw (and copy to the front buffers) only when something changed.
        if (app.TakeDirty())
        {
            app.Draw();
            screen.Invalidate();
        }
        // Waits for a free front buffer, which paces the loop to the display.
        screen.Present();
    }
    return 0;
}
