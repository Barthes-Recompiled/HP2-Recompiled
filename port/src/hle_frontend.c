/**
 * "Settings" and "Quit To Desktop" on the title menu and the pause menu: the settings pages (below, as in the
 * Philosopher's Stone port) and, on a PC, the desktop in the dashboard's place.
 *
 * The front end is the level handler sub_000FFE60(message); 0x21000004 with [0x527B9E] == 4 is its frame, and
 * sub-screen [0x527B9C] == 4 the title menu (0x1005A0, confirm at 0x1018EE): New Game, Continue (only with a save,
 * bit 0x10 of [0x547030]) and Options, rows 0..2 in [0x546F7C], drawn on the font 0x5471F8 at x 0x140 and y 0x118 +
 * 0x26 a row (0x12C and 0x152 without a save). A fourth row below them, "Quit To Desktop" (the
 * title's damaged-save screen has "Quit To Dashboard", text 0x1091), leaves the way that screen does:
 * XLaunchNewImage(NULL, NULL) (sub_0010CB5A) -> HalReturnToFirmware, which ends the process.
 *
 * The title's rows move up half a row (sub_00033D40, the text draw, while the menu runs) so the four sit where the
 * three did, clear of the copyright line. While the fourth row is chosen the title sees no up, down or confirm
 * press and no highlighted row; down from Options picks it, up goes back. Confirming it asks first: the rows give
 * way to "Quit To Desktop" / "Are you sure?" (the pause box's words) over two rows of their own, Yes and No, lit as
 * the title lights its rows; up and down choose (No first), A or START takes the lit one, B is No.
 *
 * The pause menu (page [0x52FF40] == 0) is input sub_0004E540 and drawing sub_0004E9B0: rows Resume Game, Quit Game,
 * Options (+ Level Selector with bit 0x200 of [0x527F26], + the cheat row with 0x800), the chosen one in [0x52FF48],
 * drawn on the font 0x540128 at x 0x140, y 0xD7 + 0x1E a row; [0x52FF44] == 1 is Quit Game's "Are you sure?" box
 * (title "Quit Game" at y 0xC3, A yes, B no). "Quit To Desktop" goes below the last row ([0x52FF48] -1 while it is
 * chosen, so none of the game's rows is lit or acted on) and asks in that same box, its title renamed. Presses it
 * acts on are taken with the game's own sub_00039C10(buttons, pad), as the pause menu takes them; START still
 * closes the pause (Resume's way) from the new row.
 *
 * Built into the recompile with --exclude-manual ../hpcos/src/hle_frontend.c; gen/recomp_0004.c's sub_000FFE60,
 * gen/recomp_0000.c's sub_00033D40 and gen/recomp_0001.c's sub_0004E540 / sub_0004E9B0 bodies are renamed _gen by
 * hand until then. RECOMP_QUIT_ITEM=0 leaves both menus as they were.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"

#define ARG(i) MEM32(g_esp + 4 + 4 * (i))

enum {
    kMsgFrame = 0x21000004, kFrontState = 0x527B9E, kScreen = 0x527B9C, kTitleMenu = 4, kRow = 0x546F7C,
    kFlags = 0x547030, kHasSave = 0x10, kLockout = 0x547038, kFont = 0x5471F8, kRowX = 0x140, kRowStep = 0x26, kShift = 0x13, kLastRow = 2,
    kTitleTop = 0x100, kTitleStep = 0x1E,  /* the title's rows and the port's two, closed up */
    kSndMove = 0x30003C3, kSndConfirm = 0x30003C4, kSndBack = 0x30003C5,
    kPads = 0x51E0AC, kPadStride = 0x38,  /* buttons now, +4 last frame's */
    kUp = 0x000001 | 0x100000, kDown = 0x000002 | 0x200000, kConfirm = 0x40400, kBtnA = 0x40000, kBtnB = 0x80800,
    kLeft = 0x000004 | 0x400000, kRight = 0x000008 | 0x800000,  /* as the options' sliders read them (0x4CCE7) */
    kStart = 0x400,
    kText = 0x53E754, kTxtSure = 0xDDB,  /* "Are you sure?" */
    kPausePage = 0x52FF40, kPauseSub = 0x52FF44, kPauseRow = 0x52FF48, kPauseOpts = 0x527F26, kPauseFont = 0x540128,
    kPauseY = 0xD7, kPauseStep = 0x1E, kPauseTitleY = 0xC3, kBlink = 0x51D130,
    kLit = 0xFFFFFFFFu, kLitBlink = 0xFF000000u, kGold = 0xFFDCC828u, kYellow = 0xFFFFFF00u, kDim = 0xBB808080u,
};

extern void sub_000FFE60_gen(void);
extern void sub_00033D40_gen(void);
extern void sub_0004E540_gen(void);
extern void sub_0004E9B0_gen(void);
extern void sub_0001B900(void);
extern void sub_0004E490(void);
extern void sub_00032FD0(void);
extern void sub_000330F0(void);
extern void sub_00033150(void);
extern void sub_00033390(void);
extern void sub_000333A0(void);
extern void sub_000333B0(void);
extern void sub_00033780(void);
extern void sub_00039C10(void);
extern void sub_0003BAC0(void);
extern void sub_00041A60(void);
extern void sub_0010CB5A(void);
extern void __stdcall xbox_HalReturnToFirmware(unsigned long routine);
uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

/* the port's rows below the game's, in the title and the pause menu: none chosen, Settings or Quit To Desktop */
enum { kXNone, kXSettings, kXQuit };
static int g_in_menu;       /* the title menu is running: its rows close up */
static int g_title_row;     /* the title's row of the port's chosen (kX*) */
static int g_title_ask;     /* the title asks "Are you sure?" */
static int g_title_yes;     /* ... with Yes lit (else No) */
static int g_pause_row;     /* the pause menu's row of the port's chosen (kX*) */
static int g_pause_ask;     /* ... and its "Are you sure?" box is up */
static int g_pause_draw;    /* the pause menu is drawing */

/* the settings pages: which is up and where (0 none, 1 over the title, 2 in the pause menu) */
enum { kPageSettings, kPageGameplay, kPageGraphics, kPageControls, kPageButtons };
/* the Button Mapping page's mode: its rows, waiting for a pad button (input.cpp), or asking whether to swap */
enum { kMapRows, kMapWait, kMapAsk };
static struct {
    int page, sel, top, where;
    int map_mode, map_game, map_button, map_other;  /* the game button asked for, the pad button got, its owner */
} g_pg;
extern int app_PageOptionCount(int page);  /* renderer/app/app.cpp: 0 Gameplay, 1 Graphics, 2 Controls */
extern const char *app_PageOptionLabel(int page, int i);
extern const char *app_PageOptionValue(int page, int i);
extern void app_PageOptionStep(int page, int i, int dir);
extern void app_PadCaptureStart(void);  /* renderer/app/input.cpp: the Button Mapping page's wait and the remapping */
extern int app_PadCaptureResult(int *button);
extern const char *app_PadInputLabel(int i);
extern int app_PadInputCount(void);
extern int app_PadMapGet(int game);
extern void app_PadMapSet(int game, int button, int swap);
extern void app_PadMapReset(void);

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_QUIT_ITEM");
        on = !(e && *e == '0');
    }
    return on;
}

/* a recompiled function on this guest thread: thiscall's `this` in ecx, the arguments pushed right to left and a
 * return address; esp and the callee-saved registers as before whoever pops */
static uint32_t call_guest(void (*fn)(void), uint32_t this_, int n, const uint32_t *args)
{
    uint32_t s_esp = g_esp, s_ebx = g_ebx, s_esi = g_esi, s_edi = g_edi, s_ebp = g_ebp;
    uint32_t sp = s_esp;
    for (int i = n; i-- > 0;) {
        sp -= 4;
        MEM32(sp) = args[i];
    }
    sp -= 4;
    MEM32(sp) = 0;
    g_esp = sp;
    g_ecx = this_;
    fn();
    g_esp = s_esp, g_ebx = s_ebx, g_esi = s_esi, g_edi = s_edi, g_ebp = s_ebp;
    return g_eax;
}

static uint32_t fb(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void sound(uint32_t id)
{
    uint32_t a[2] = {id, 0x3F800000u};  /* volume 1.0 */
    call_guest(sub_00041A60, 0, 2, a);
}

/* the game's own taking of a press: last frame's bits of `buttons` on pad 0 become this frame's */
static void take(uint32_t buttons)
{
    uint32_t a[2] = {buttons, 0};
    call_guest(sub_00039C10, 0, 2, a);
}

static void quit_to_desktop(void)
{
    fprintf(stderr, "[MENU] Quit To Desktop\n");
    uint32_t a[2] = {0, 0};
    call_guest(sub_0010CB5A, 0, 2, a);  /* XLaunchNewImage(NULL, NULL): does not come back */
    xbox_HalReturnToFirmware(2);
}

/* the presses this frame on any pad, as sub_000FF7B0 reads them */
static uint32_t pressed(void)
{
    uint32_t p = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t now = MEM32(kPads + kPadStride * i), was = MEM32(kPads + kPadStride * i + 4);
        p |= (now ^ was) & now;
    }
    return p & ~MEM32(kLockout);
}

/* a UTF-16 string in guest memory, as the title's text tables hold them */
static uint32_t guest_text(const char *s, uint32_t *va)
{
    if (!*va) {
        unsigned n = 0;
        while (s[n]) n++;
        *va = xbox_HeapAlloc((n + 1) * 2, 16);
        if (!*va) return 0;
        for (unsigned i = 0; i <= n; i++) MEM16(*va + 2 * i) = (uint8_t)s[i];
    }
    return *va;
}

/* a line that changes (a setting's value) through a buffer of its own, rewritten each time */
static uint32_t guest_line(const char *s, uint32_t *va)
{
    if (!*va) *va = xbox_HeapAlloc(2 * 100, 16);
    if (!*va) return 0;
    unsigned i = 0;
    for (; s[i] && i < 99; i++) MEM16(*va + 2 * i) = (uint8_t)s[i];
    MEM16(*va + 2 * i) = 0;
    return *va;
}

static uint32_t quit_text(void)
{
    static uint32_t va;
    return guest_text("Quit To Desktop", &va);
}

static uint32_t settings_text(void)
{
    static uint32_t va;
    return guest_text("Settings", &va);
}

static uint32_t extra_text(int kind) { return kind == kXSettings ? settings_text() : quit_text(); }

static uint32_t yes_text(void)
{
    static uint32_t va;
    return guest_text("Yes", &va);
}

static uint32_t no_text(void)
{
    static uint32_t va;
    return guest_text("No", &va);
}

static uint32_t empty_text(void)
{
    static uint32_t va;
    return guest_text("", &va);
}

/* a line of the game's text table (thiscall on 0x53E754) */
static uint32_t game_text(uint32_t id)
{
    uint32_t a[1] = {id};
    return call_guest(sub_0003BAC0, kText, 1, a);
}

static void draw_text_at(uint32_t font, uint32_t colour, uint32_t x, uint32_t y, uint32_t text)
{
    if (!text) return;
    uint32_t col[1] = {colour};
    call_guest(sub_00032FD0, font, 1, col);
    uint32_t a[3] = {x, y, text};
    call_guest(sub_00033D40_gen, font, 3, a);
}

static void draw_text(uint32_t font, uint32_t colour, uint32_t y, uint32_t text)
{
    draw_text_at(font, colour, kRowX, y, text);
}

/* a row lit as the pause menu lights its own (0x4EB90): blinking white and black */
static uint32_t lit_colour(void) { return (MEM8(kBlink) & 0x10) ? kLit : kLitBlink; }

/* the pause menu's rows (0x4E626) */
static int pause_rows(void)
{
    uint32_t opts = MEM32(kPauseOpts);
    return 3 + !!(opts & 0x200) + !!(opts & 0x800);
}

/* a title row's index from the y the title draws it at, -1 not one: 0x118 + 0x26 a row, 0x12C without a save */
static int title_row_index(uint32_t y)
{
    uint32_t top = (MEM8(kFlags) & kHasSave) ? 0x118 : 0x12C;
    if (y < top || (y - top) % kRowStep) return -1;
    uint32_t i = (y - top) / kRowStep;
    return i <= ((MEM8(kFlags) & kHasSave) ? 2u : 1u) ? (int)i : -1;
}

/* the title's rows (2 or 3) and the port's two, closed up into the room the three had */
static uint32_t title_y(int i)
{
    int has = !!(MEM8(kFlags) & kHasSave);
    return kTitleTop + (has ? 0 : kTitleStep / 2) + kTitleStep * (uint32_t)i;
}

static int title_game_rows(void) { return (MEM8(kFlags) & kHasSave) ? 3 : 2; }

/* ── the settings pages ──────────────────────────────────────────────────────────────────────────────────────────
 * "Settings" (the title's and the pause menu's row of the port's) opens Gameplay, Graphics and Controls: the same
 * settings the F10 panel changes (app_PageOption*, app.cpp), stepped with left / right or A and saved to hpcos.toml.
 * Controls opens with Button Mapping: each game button and the pad button it reads (pad_*); A waits for a pad button
 * (input.cpp; the game sees the pad at rest meanwhile), one already in use asks whether to swap the two, and holding
 * B, Escape or ten idle seconds cancel. A page is the pause box (sub_0004E490: a half-black fill, a grey frame) made
 * larger, its name in the box's yellow and its rows in the pause menu's gold, the chosen one blinking as there, with
 * the box's own button hint; over the title a darker fill first hides the menu behind. B goes back a page. */
#define PAGE_ROWS_SHOWN 9
#define PAGE_X0 40
#define PAGE_Y0 44
#define PAGE_X1 600
#define PAGE_Y1 412
#define PAGE_NAME_Y 62
#define PAGE_ROW_Y 112
#define PAGE_STEP 28

typedef struct { char text[96]; int enabled; } PageRow;

static int page_rows(int page, PageRow *rows, int max)
{
    int n = 0;
    if (page == kPageSettings) {
        static const char *const kSub[3] = {"Gameplay", "Graphics", "Controls"};
        for (; n < 3; n++) {
            snprintf(rows[n].text, sizeof rows[n].text, "%s", kSub[n]);
            rows[n].enabled = 1;
        }
    } else if (page == kPageButtons && g_pg.map_mode == kMapAsk) {
        /* the pad button got already plays another game button: swap the two, or leave both */
        snprintf(rows[0].text, sizeof rows[0].text, "%s is used by %s", app_PadInputLabel(g_pg.map_button),
                 app_PadInputLabel(g_pg.map_other));
        snprintf(rows[1].text, sizeof rows[1].text, "Swap, %s gets %s", app_PadInputLabel(g_pg.map_other),
                 app_PadInputLabel(app_PadMapGet(g_pg.map_game)));
        snprintf(rows[2].text, sizeof rows[2].text, "Cancel");
        rows[0].enabled = 0, rows[1].enabled = rows[2].enabled = 1;
        n = 3;
    } else if (page == kPageButtons) {
        /* each game button and the pad button it reads, then the defaults */
        for (int i = 0; n < max && i < app_PadInputCount(); i++, n++) {
            int wait = g_pg.map_mode == kMapWait && i == g_pg.map_game;
            snprintf(rows[n].text, sizeof rows[n].text, "%s   %s", app_PadInputLabel(i),
                     wait ? "press a button" : app_PadInputLabel(app_PadMapGet(i)));
            rows[n].enabled = 1;
        }
        if (n < max) {
            snprintf(rows[n].text, sizeof rows[n].text, "Reset to defaults");
            rows[n].enabled = 1, n++;
        }
    } else {
        int opts = page - kPageGameplay;
        if (page == kPageControls && n < max) {  /* Controls opens with the Button Mapping page */
            snprintf(rows[n].text, sizeof rows[n].text, "Button mapping");
            rows[n].enabled = 1, n++;
        }
        for (int i = 0; n < max && i < app_PageOptionCount(opts); i++, n++) {
            snprintf(rows[n].text, sizeof rows[n].text, "%s   %s", app_PageOptionLabel(opts, i),
                     app_PageOptionValue(opts, i));
            rows[n].enabled = 1;
        }
    }
    return n;
}

static const char *page_title(int page)
{
    switch (page) {
    case kPageSettings: return "Settings";
    case kPageGameplay: return "Gameplay";
    case kPageGraphics: return "Graphics";
    case kPageButtons: return "Button Mapping";
    default: return "Controls";
    }
}

static void page_open_at(int page, int where)
{
    PageRow rows[64];
    g_pg.map_mode = kMapRows;
    int n = page_rows(page, rows, 64);
    g_pg.page = page, g_pg.sel = -1, g_pg.top = 0, g_pg.where = where;
    for (int i = 0; i < n && g_pg.sel < 0; i++)
        if (rows[i].enabled) g_pg.sel = i;
    fprintf(stderr, "[MENU] page %s\n", page_title(page));
}

/* back out of a page: Button Mapping to Controls, a sub-page to Settings, Settings to its menu */
static void page_back(void)
{
    if (g_pg.page == kPageButtons) {
        if (g_pg.map_mode == kMapAsk) {  /* the swap question's B: as Cancel */
            g_pg.map_mode = kMapRows, g_pg.sel = g_pg.map_game;
            return;
        }
        page_open_at(kPageControls, g_pg.where);
        g_pg.sel = 0;
        return;
    }
    if (g_pg.page != kPageSettings) {
        int from = g_pg.page;
        page_open_at(kPageSettings, g_pg.where);
        g_pg.sel = from - kPageGameplay;
        return;
    }
    g_pg.where = 0;
}

/* the Button Mapping page's answer from the pad (input.cpp): a free button is taken, one in use asks to swap */
static void map_wait(void)
{
    int button, r = app_PadCaptureResult(&button);
    if (!r) return;
    g_pg.map_mode = kMapRows;
    if (r < 0) {
        sound(kSndBack);
        return;
    }
    g_pg.map_other = -1;
    for (int j = 0; j < app_PadInputCount(); j++)
        if (j != g_pg.map_game && app_PadMapGet(j) == button) g_pg.map_other = j;
    if (g_pg.map_other < 0 || app_PadMapGet(g_pg.map_game) == button) {
        app_PadMapSet(g_pg.map_game, button, 0);
        sound(kSndConfirm);
        return;
    }
    g_pg.map_button = button, g_pg.map_mode = kMapAsk, g_pg.sel = 2, g_pg.top = 0;  /* Cancel lit */
    sound(kSndMove);
}

/* a page's input step on this frame's presses */
static void page_input(uint32_t p)
{
    if (g_pg.page == kPageButtons && g_pg.map_mode == kMapWait) {  /* the game sees the pad at rest meanwhile */
        map_wait();
        return;
    }
    PageRow rows[64];
    int n = page_rows(g_pg.page, rows, 64);
    int a = !!(p & kBtnA), left = !!(p & kLeft), right = !!(p & kRight);
    if (p & kBtnB) {
        sound(kSndBack);
        page_back();
        return;
    }
    if ((p & (kUp | kDown)) && n) {
        int dir = (p & kDown) ? 1 : -1, c = g_pg.sel;
        for (int k = 0; k < n; k++) {
            c = (c + dir + n) % n;
            if (rows[c].enabled) break;
        }
        if (c != g_pg.sel && rows[c].enabled) {
            g_pg.sel = c;
            sound(kSndMove);
        }
    }
    if (g_pg.sel >= 0 && g_pg.sel < n) {
        if (g_pg.sel < g_pg.top) g_pg.top = g_pg.sel;
        if (g_pg.sel >= g_pg.top + PAGE_ROWS_SHOWN) g_pg.top = g_pg.sel - PAGE_ROWS_SHOWN + 1;
    }
    if (g_pg.sel < 0 || g_pg.sel >= n || !rows[g_pg.sel].enabled) return;
    if (g_pg.page == kPageButtons) {
        if (!a) return;
        if (g_pg.map_mode == kMapAsk) {
            if (g_pg.sel == 1) app_PadMapSet(g_pg.map_game, g_pg.map_button, 1);
            sound(g_pg.sel == 1 ? kSndConfirm : kSndBack);
            g_pg.map_mode = kMapRows, g_pg.sel = g_pg.map_game;
        } else if (g_pg.sel == app_PadInputCount()) {
            app_PadMapReset();
            sound(kSndConfirm);
        } else {
            g_pg.map_game = g_pg.sel, g_pg.map_mode = kMapWait;
            app_PadCaptureStart();
            sound(kSndConfirm);
        }
        return;
    }
    if (g_pg.page == kPageSettings) {
        if (!a) return;
        sound(kSndConfirm);
        page_open_at(kPageGameplay + g_pg.sel, g_pg.where);
        return;
    }
    if (g_pg.page == kPageControls && g_pg.sel == 0) {
        if (!a) return;
        sound(kSndConfirm);
        page_open_at(kPageButtons, g_pg.where);
        return;
    }
    if (left || right || a) {
        app_PageOptionStep(g_pg.page - kPageGameplay, g_pg.page == kPageControls ? g_pg.sel - 1 : g_pg.sel,
                           left ? -1 : 1);
        sound(kSndMove);
    }
}

/* the pause box's font state (0x4E9CE): centred, then the scale */
static void font_setup(uint32_t font, float scale)
{
    uint32_t a[2] = {0, 0};
    call_guest(sub_000333A0, font, 1, a);
    a[0] = 2, a[1] = 0;
    call_guest(sub_000333B0, font, 2, a);
    a[0] = 1;
    call_guest(sub_00033390, font, 1, a);
    a[0] = 0xFFFFFFFFu;
    call_guest(sub_00033150, font, 1, a);
    a[0] = fb(scale);
    call_guest(sub_00033780, font, 1, a);
}

static void fill(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t colour)
{
    uint32_t a[5] = {x0, y0, x1, y1, colour};
    call_guest(sub_0001B900, 0, 5, a);
}

static void page_draw(void)
{
    uint32_t font = kPauseFont, name_font = g_pg.where == 1 ? kFont : kPauseFont;  /* kFont has no digits */
    PageRow rows[64];
    int n = page_rows(g_pg.page, rows, 64);
    /* the title's logo behind dimmed; the game behind the pause darker than the pause box alone leaves it */
    fill(0, 0, 640, 480, g_pg.where == 1 ? 0xE0000000u : 0x70000000u);
    uint32_t box[4] = {PAGE_X0, PAGE_Y0, PAGE_X1, PAGE_Y1};
    call_guest(sub_0004E490, 0, 4, box);
    font_setup(name_font, 1.0f);
    static uint32_t name_va[5];
    draw_text(name_font, kYellow, PAGE_NAME_Y, guest_text(page_title(g_pg.page), &name_va[g_pg.page]));
    font_setup(font, 0.85f);
    int shown = n < PAGE_ROWS_SHOWN ? n : PAGE_ROWS_SHOWN;
    uint32_t row0 = PAGE_ROW_Y + PAGE_STEP * (uint32_t)(PAGE_ROWS_SHOWN - shown) / 2;
    static uint32_t row_va[PAGE_ROWS_SHOWN];
    for (int r = 0; r < shown && g_pg.top + r < n; r++) {
        int i = g_pg.top + r;
        uint32_t col = !rows[i].enabled ? kDim : i == g_pg.sel ? lit_colour() : kGold;
        draw_text(font, col, row0 + PAGE_STEP * (uint32_t)r, guest_line(rows[i].text, &row_va[r]));
    }
    static uint32_t more_va;
    if (g_pg.top > 0) draw_text(font, kGold, row0 - 18, guest_text("...", &more_va));
    if (g_pg.top + PAGE_ROWS_SHOWN < n)
        draw_text(font, kGold, row0 + PAGE_STEP * (uint32_t)shown - 8, guest_text("...", &more_va));
    font_setup(font, 0.75f);
    if (g_pg.page == kPageButtons && g_pg.map_mode == kMapWait) {
        static uint32_t wait_va;
        draw_text(font, kYellow, PAGE_Y1 - 52,
                  guest_text("Press a button for it. Hold B or press Esc to cancel", &wait_va));
    }
    static uint32_t hint_va;
    draw_text(font, kLit, PAGE_Y1 - 30, guest_text("A  Select      B  Back", &hint_va));
    /* the port everything here stands on */
    static uint32_t thanks_va;
    font_setup(font, 0.5f);
    draw_text(font, 0xFFA0B0CCu, PAGE_Y1 + 26,
              guest_text("Huge thanks to MiddleDucky, whose wonderful port makes all of this possible", &thanks_va));
    call_guest(sub_000330F0, font, 0, NULL);  /* the scale back to 1, as the pause box leaves it */
}

/* text draw (this = font; x, y, text) */
void sub_00033D40(void)
{
    if (g_in_menu && g_ecx == kFont && ARG(0) == kRowX && title_row_index(ARG(1)) >= 0) {
        /* the title menu's rows closed up for the port's two below; none while it asks or a page is up */
        ARG(1) = title_y(title_row_index(ARG(1)));
        if (g_title_ask || g_pg.where == 1) ARG(2) = empty_text();
    } else if (g_in_menu && g_pg.where == 1) {
        ARG(2) = empty_text();  /* a page over the title: none of the title's text (rows, copyright) */
    } else if (g_pause_draw && g_ecx == kPauseFont && ARG(0) == kRowX) {
        uint32_t y = ARG(1), last = kPauseY + kPauseStep * (pause_rows() - 1);
        if (g_pause_ask && y == kPauseTitleY) {
            ARG(2) = quit_text();  /* the box's title: "Quit To Desktop", not "Quit Game" */
        } else if (MEM32(kPauseSub) == 0 && y == last) {
            uint32_t s_esp = g_esp;
            sub_00033D40_gen();
            uint32_t ret_esp = g_esp;
            g_esp = s_esp;
            /* the port's rows under the last, lit as the pause menu lights its own (0x4EB90) */
            for (int k = kXSettings; k <= kXQuit; k++)
                draw_text(kPauseFont, g_pause_row == k ? lit_colour() : kGold, last + kPauseStep * (uint32_t)k,
                          extra_text(k));
            g_esp = ret_esp;
            return;
        }
    }
    sub_00033D40_gen();
}

void sub_000FFE60(void)
{
    if (!enabled() || ARG(0) != kMsgFrame || MEM16(kFrontState) != 4) {
        /* the handler's other messages (0x10000004 and the rest) come every frame too: they leave the row alone */
        sub_000FFE60_gen();
        return;
    }
    if (MEM16(kScreen) != kTitleMenu) {
        g_title_row = g_title_ask = 0;
        g_pg.where = g_pg.where == 1 ? 0 : g_pg.where;
        sub_000FFE60_gen();
        return;
    }
    uint32_t p = pressed(), held[4], hide = 0;
    if (g_pg.where == 1) {
        hide = kUp | kDown | kConfirm | kBtnB | kLeft | kRight;  /* the page has the pad */
        page_input(p);
    } else if (g_title_ask) {
        hide = kUp | kDown | kConfirm | kBtnB;  /* B would leave the menu */
        if ((p & kConfirm) && g_title_yes) {
            sound(kSndConfirm);
            quit_to_desktop();
        } else if (p & (kConfirm | kBtnB)) {
            g_title_ask = 0;
            sound(kSndBack);
        } else if (p & (kUp | kDown)) {
            g_title_yes = !g_title_yes;
            sound(kSndMove);
        }
    } else if (!g_title_row) {
        if ((p & kDown) && (int32_t)MEM32(kRow) == kLastRow) {
            g_title_row = kXSettings;
            sound(kSndMove);
            hide = kUp | kDown | kConfirm;
        }
    } else {
        hide = kUp | kDown | kConfirm;
        if (p & kConfirm) {
            sound(kSndConfirm);
            if (g_title_row == kXSettings) {
                page_open_at(kPageSettings, 1);
            } else {
                g_title_ask = 1;
                g_title_yes = 0;
            }
        } else if (p & kUp) {
            if (g_title_row == kXQuit) {
                g_title_row = kXSettings;
            } else {
                g_title_row = kXNone;
                MEM32(kRow) = kLastRow;
            }
            sound(kSndMove);
        } else if ((p & kDown) && g_title_row == kXSettings) {
            g_title_row = kXQuit;
            sound(kSndMove);
        }
    }
    if (g_title_row) MEM32(kRow) = (uint32_t)-1;  /* no title row lit */
    if (hide)
        for (int i = 0; i < 4; i++) {
            /* no new press of these reaches the title this frame */
            uint32_t was = kPads + kPadStride * i + 4;
            held[i] = MEM32(was);
            MEM32(was) |= MEM32(was - 4) & hide;
        }

    uint32_t s_esp = g_esp;
    g_in_menu = 1;
    sub_000FFE60_gen();
    g_in_menu = 0;

    if (hide)
        for (int i = 0; i < 4; i++) MEM32(kPads + kPadStride * i + 4) = held[i];
    /* the title may have left the menu this frame (Options, New Game) */
    if (MEM16(kScreen) != kTitleMenu) {
        g_title_row = g_title_ask = 0;
        return;
    }
    uint32_t ret_esp = g_esp;
    g_esp = s_esp;  /* our calls below the frame the title's handler had */
    if (g_pg.where == 1) {
        page_draw();
    } else if (g_title_ask) {
        /* in the rows' places: the question in the pause box's yellow (0x4EADC), then Yes and No lit and dimmed as
         * the title's own rows */
        uint32_t y = title_y(0);
        draw_text(kFont, kYellow, y, quit_text());
        draw_text(kFont, kYellow, y + kTitleStep, game_text(kTxtSure));
        draw_text(kFont, g_title_yes ? kLit : kDim, y + 2 * kTitleStep, yes_text());
        draw_text(kFont, g_title_yes ? kDim : kLit, y + 3 * kTitleStep, no_text());
    } else {
        for (int k = kXSettings; k <= kXQuit; k++)
            draw_text(kFont, g_title_row == k ? kLit : kDim, title_y(title_game_rows() - 1 + k), extra_text(k));
    }
    g_esp = ret_esp;
}

/* the pause menu's input step */
void sub_0004E540(void)
{
    /* xboxrecomp debug: RECOMP_LEVEL_SELECT=1 shows the developers' Level Selector row (bit 0x200 of [0x527F26]):
     * every level with its start points by name. The bit is in the progress block, so a save keeps it */
    static int level_select = -1;
    if (level_select < 0) {
        const char *e = getenv("RECOMP_LEVEL_SELECT");
        level_select = e && *e == '1';
    }
    if (level_select) MEM16(kPauseOpts) |= 0x200;
    if (!enabled() || MEM32(kPausePage) != 0) {
        g_pause_row = g_pause_ask = 0;
        if (g_pg.where == 2) g_pg.where = 0;
        sub_0004E540_gen();
        return;
    }
    uint32_t now = MEM32(kPads), p = (now ^ MEM32(kPads + 4)) & now;
    if (g_pg.where == 2) {
        /* a page has the pad; the pause menu's own step waits */
        take(p);
        page_input(p);
        if (g_pg.where == 0) g_pause_row = kXSettings, MEM32(kPauseRow) = (uint32_t)-1;  /* back on Settings */
        g_esp += 4;  /* the step's ret */
        return;
    }
    /* a pause opened afresh (sub_0004DE80) starts on Resume with no box */
    if (g_pause_row && MEM32(kPauseRow) != (uint32_t)-1) g_pause_row = g_pause_ask = 0;
    if (g_pause_ask && MEM32(kPauseSub) != 1) g_pause_ask = 0;

    int n = pause_rows();
    if (g_pause_ask) {
        /* the box's A and B are ours; START closes the pause as with Quit Game's */
        if (p & kBtnA) {
            take(kBtnA);
            sound(kSndConfirm);
            quit_to_desktop();
        } else if (p & kBtnB) {
            take(kBtnB);
            sound(kSndBack);
            MEM32(kPauseSub) = 0;
            g_pause_ask = 0;
        }
    } else if (!g_pause_row) {
        if ((p & kDown) && MEM32(kPauseSub) == 0 && (int32_t)MEM32(kPauseRow) == n - 1) {
            take(kDown);
            sound(kSndMove);
            g_pause_row = kXSettings;
        }
    } else if (MEM32(kPauseSub) == 0) {
        if (p & kStart) {
            /* START resumes from any row: hand it Resume's */
            g_pause_row = kXNone;
            MEM32(kPauseRow) = 0;
        } else if (p & kBtnA) {
            take(kBtnA);
            sound(kSndConfirm);
            if (g_pause_row == kXSettings) {
                page_open_at(kPageSettings, 2);
            } else {
                MEM32(kPauseSub) = 1;
                g_pause_ask = 1;
            }
        } else if (p & kUp) {
            take(kUp | kDown);
            sound(kSndMove);
            if (g_pause_row == kXQuit) {
                g_pause_row = kXSettings;
            } else {
                g_pause_row = kXNone;
                MEM32(kPauseRow) = n - 1;
            }
        } else if (p & kDown) {
            take(kDown);
            if (g_pause_row == kXSettings) {
                g_pause_row = kXQuit;
                sound(kSndMove);
            }
        }
    }
    if (g_pause_row) MEM32(kPauseRow) = (uint32_t)-1;  /* none of the game's rows lit or acted on */
    if (g_pg.where == 2) {  /* a page opened: the pause menu's own step waits */
        g_esp += 4;          /* the step's ret */
        return;
    }
    sub_0004E540_gen();
}

/* the pause menu's drawing; a page in its place */
void sub_0004E9B0(void)
{
    if (enabled() && MEM32(kPausePage) == 0 && g_pg.where == 2) {
        page_draw();
        g_esp += 4;  /* the drawing's ret */
        return;
    }
    g_pause_draw = enabled() && MEM32(kPausePage) == 0;
    sub_0004E9B0_gen();
    g_pause_draw = 0;
}
