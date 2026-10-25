/*
 * hpcos_trainer.exe: the debug trainer. Runs beside hpcos.exe (start either
 * first, or both with run_with_trainer.sh) and talks to it through the shared
 * block in trainer_shm.h; the game carries the requests out on its own thread
 * (hle_trainer.c).
 *
 *   Noclip         Harry flies: left stick along the camera, RT up, LT down,
 *                  A held 4x as fast; walls, doors and floors do not stop him
 *   Infinite health, every spell
 *   Collection     every wizard card (the title's way: more health each 10th), health to the maximum, the
 *                  maximum to the cap, beans, a bigger bag -- and taking the cards back, or Undo: all back as before
 *                  the first change
 *   Levels         any record of the level table (the pause menu's level selector's way), test maps included
 *   Positions      three saved spots to jump back to
 *   Debug report   run\reports\<time>: guest memory, the progress block, logs (copied from here, so a hung
 *                  game still gives them), every thread's stack, the game's state and a frame capture (from
 *                  the game)
 *
 * Hotkeys (work while the game has focus): Ctrl+Shift+N noclip,
 * Ctrl+Shift+G infinite health, Ctrl+Shift+1..3 save spot, Ctrl+1..3 go,
 * Ctrl+Shift+R debug report.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../trainer_shm.h"

enum {
    ID_TIMER = 1, ID_NOCLIP = 100, ID_SPEED, ID_GOD, ID_SPELLS,
    ID_SAVE0 = 120, ID_GO0 = 130, ID_STATUS = 140, ID_POS, ID_SPOT0 = 150,
    ID_CARDS = 160, ID_CARDS_RM, ID_FILL, ID_HEALTH_MAX, ID_BEANS, ID_BAG, ID_BEANS_VALUE, ID_BEANS_SET, ID_UNDO,
    ID_COLLECT, ID_CARDS_NOT_HARRY, ID_LEVEL = 175, ID_START, ID_WARP, ID_LEVEL_NOW, ID_REPLAY,
    ID_REPORT = 180, ID_REPORT_MEM, ID_REPORT_STATUS,
    HK_NOCLIP = 1, HK_GOD, HK_SAVE0, HK_GO0 = 10, HK_REPORT = 20,
    WM_REPORT_DONE = WM_APP + 1,
};

/* the level table's names (records from 0x1E3DE8), by index */
static const char *const kLevels[HPCOS_LEVELS] = {
    "The Burrow", "DADA Expeliamus", "Library", "Quiddich World Cup", "Borgin & Burkes", "Diagon Alley",
    "Gringotts", "Mulpeppers", "Leaky Cauldron", "Flourish & Blotts", "Gambol & Japes", "Magical Menagerie",
    "Whomping Willow", "Whomping Imp Area", "Whomping Wiggenweld", "Whomping Gytrash Area", "Great Hall", "DADA",
    "DADA Fumos", "Pre Library", "Library Potions", "Dumbledores office", "Incendio", "Avifors Challenge",
    "Herbology", "Chamber Of Secrets", "Hogwarts Exterior", "Entrance Hall", "House Points Meter",
    "Grand Staircase", "Gryffindor Common Room", "Pink Room", "DiagonAlley intro", "Stealth Dungeons",
    "Stealth Library", "Test Map DP", "Test Map JV", "Test Map KA", "Test Map MT", "Julians Folly", "The Asylum",
    "Test Map MD", "Test Map PB", "Test Map JS", "Nojland", "Test Map AW", "Test Map NB", "Test Map CA",
    "Test Map DS", "Cutscene Test", "Animation Viewer", "Game Titles", "Game Credits", "To Be Continued",
    "Quidd win screen", "Aragog Arena", "Aragog Forest", "Aragog Lair", "Text Display Doodah",
};

static HpcosTrainerShm *g_t;
static HWND g_wnd, g_noclip, g_speed, g_god, g_spells, g_status, g_pos, g_spot[3], g_collect, g_undo;
static HWND g_beans_value, g_level, g_start, g_replay, g_level_now, g_report_mem, g_report_status, g_report_btn;
static float g_spots[3][3];
static int g_spot_set[3];
static uint32_t g_last_frame, g_stale;
static char g_dir[MAX_PATH];  /* where this exe is: run\ */
static volatile LONG g_reporting;
static char g_report_msg[512];

static const char *level_name(unsigned i)
{
    return i < HPCOS_LEVELS ? kLevels[i] : "?";
}

static int spells_known(uint32_t m)
{
    int n = 0;
    for (m &= HPCOS_ALL_SPELLS; m; m &= m - 1) n++;
    return n;
}

static int attach(void)
{
    if (g_t) {
        /* the game restarted with a new block: let go of the old one */
        if (g_t->magic == HPCOS_TRAINER_MAGIC) return 1;
        UnmapViewOfFile(g_t);
        g_t = NULL;
    }
    HANDLE m = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, HPCOS_TRAINER_NAME);
    if (!m) return 0;
    HpcosTrainerShm *t = (HpcosTrainerShm *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HpcosTrainerShm));
    CloseHandle(m);
    if (!t) return 0;
    if (t->magic != HPCOS_TRAINER_MAGIC || t->version != HPCOS_TRAINER_VERSION) {
        UnmapViewOfFile(t);
        return 0;
    }
    g_t = t;
    return 1;
}

static int checked(HWND box)
{
    return SendMessageA(box, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void push_settings(void)
{
    if (!attach()) return;
    char buf[32];
    GetWindowTextA(g_speed, buf, sizeof buf);
    g_t->fly_speed = (float)atof(buf);
    g_t->noclip = checked(g_noclip);
    g_t->god = checked(g_god);
    g_t->all_spells = checked(g_spells);
}

static void show_spot(int i)
{
    char b[96];
    if (g_spot_set[i]) snprintf(b, sizeof b, "%.1f  %.1f  %.1f", g_spots[i][0], g_spots[i][1], g_spots[i][2]);
    else snprintf(b, sizeof b, "(empty)");
    SetWindowTextA(g_spot[i], b);
}

static void save_spot(int i)
{
    if (!attach() || !g_t->player) return;
    for (int k = 0; k < 3; k++) g_spots[i][k] = g_t->pos[k];
    g_spot_set[i] = 1;
    show_spot(i);
}

static void go_spot(int i)
{
    if (!attach() || !g_spot_set[i]) return;
    for (int k = 0; k < 3; k++) g_t->tp_pos[k] = g_spots[i][k];
    g_t->tp_seq++;
}

static void toggle(HWND box)
{
    SendMessageA(box, BM_SETCHECK, checked(box) ? BST_UNCHECKED : BST_CHECKED, 0);
    push_settings();
}

static void warp(void)
{
    if (!attach()) return;
    int sel = (int)SendMessageA(g_level, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= HPCOS_LEVELS) return;
    char buf[16];
    GetWindowTextA(g_start, buf, sizeof buf);
    g_t->warp_level = (uint16_t)sel;
    g_t->warp_start = (uint16_t)atoi(buf);
    g_t->warp_replay = checked(g_replay);
    g_t->warp_seq++;
}

/* ── debug report ─────────────────────────────────────────────────────────── */

static void report_line(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
}

/* guest memory through ReadProcessMemory, page by page (holes read as zero) */
static size_t dump_guest(HANDLE h, uint64_t off, uint32_t va, uint32_t size, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    static unsigned char page[65536];
    size_t got = 0;
    for (uint32_t o = 0; o < size; o += sizeof page) {
        uint32_t n = size - o < sizeof page ? size - o : sizeof page;
        SIZE_T r = 0;
        memset(page, 0, n);
        if (!ReadProcessMemory(h, (void *)(uintptr_t)(va + o + off), page, n, &r)) {
            /* a hole inside: smaller pieces */
            for (uint32_t k = 0; k < n; k += 4096) {
                SIZE_T rr = 0;
                if (ReadProcessMemory(h, (void *)(uintptr_t)(va + o + k + off), page + k, 4096, &rr)) r += rr;
            }
        }
        got += r;
        fwrite(page, 1, n, f);
    }
    fclose(f);
    return got;
}

/* the newest logs\runs\<prefix>*.log the launcher made (one per run), else the old single file beside the exe */
static const char *run_log(const char *prefix, const char *fallback, char *out, size_t n)
{
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    FILETIME best = {0, 0};
    snprintf(out, n, "%s", fallback);
    snprintf(pat, sizeof pat, "%s\\logs\\runs\\%s*.log", g_dir, prefix);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (CompareFileTime(&fd.ftLastWriteTime, &best) > 0) {
            best = fd.ftLastWriteTime;
            snprintf(out, n, "logs\\runs\\%s", fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

static int copy_in(const char *from_rel, const char *dir, const char *name)
{
    char from[MAX_PATH], to[MAX_PATH];
    snprintf(from, sizeof from, "%s\\%s", g_dir, from_rel);
    snprintf(to, sizeof to, "%s\\%s", dir, name);
    /* the logs are still being written: read them shared */
    HANDLE in = CreateFileA(from, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE) return 0;
    FILE *out = fopen(to, "wb");
    if (!out) { CloseHandle(in); return 0; }
    static char buf[65536];
    DWORD r;
    while (ReadFile(in, buf, sizeof buf, &r, NULL) && r) fwrite(buf, 1, r, out);
    fclose(out);
    CloseHandle(in);
    return 1;
}

/* the newest folder in run\captures made since `since` (a capture the game wrote for the report) */
static int newest_capture(FILETIME since, char *name, size_t len)
{
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\captures\\*", g_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FILETIME best = since;
    int found = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        if (CompareFileTime(&fd.ftCreationTime, &best) >= 0) {
            best = fd.ftCreationTime;
            snprintf(name, len, "%s", fd.cFileName);
            found = 1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static DWORD WINAPI report_worker(LPVOID arg)
{
    int with_memory = (int)(INT_PTR)arg;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[32], dir[MAX_PATH], path[MAX_PATH];
    snprintf(stamp, sizeof stamp, "%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    snprintf(path, sizeof path, "%s\\reports", g_dir);
    CreateDirectoryA(path, NULL);
    snprintf(dir, sizeof dir, "%s\\reports\\%s", g_dir, stamp);
    CreateDirectoryA(dir, NULL);
    FILETIME t0;
    GetSystemTimeAsFileTime(&t0);

    snprintf(path, sizeof path, "%s\\report.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(g_report_msg, sizeof g_report_msg, "Could not create %s", dir);
        PostMessageA(g_wnd, WM_REPORT_DONE, 0, 0);
        return 0;
    }
    report_line(f, "hpcos debug report %s", stamp);
    int game = attach();
    uint32_t asked = 0;
    if (game) {
        HpcosTrainerShm *t = g_t;
        report_line(f, "game pid %u, guest -> host offset %+lld", t->pid, (long long)t->mem_offset);
        report_line(f, "level %u (%s)", t->level, level_name(t->level));
        report_line(f, "Harry's updates %u, player %08X, position %.3f %.3f %.3f yaw %.3f%s", t->frame, t->player,
                    t->pos[0], t->pos[1], t->pos[2], t->yaw, t->noclip_on ? ", noclip" : "");
        report_line(f, "health %d / %d, cards %u (trainer gave %u), beans %d / %d, spells %08X (%d of 8)",
                    t->health, t->health_max, t->cards, t->cards_given, t->beans, t->beans_max, t->spells,
                    spells_known(t->spells));
        report_line(f, "trainer settings: noclip %u god %u all spells %u fly speed %.1f, undo kept %u", t->noclip,
                    t->god, t->all_spells, t->fly_speed, t->undo_ready);
        /* the game's part, on its own thread */
        snprintf((char *)t->report_dir, sizeof t->report_dir, "%s", dir);
        asked = ++t->report_seq;
    } else {
        report_line(f, "game: no shared block (hpcos.exe not running, or an older build)");
    }

    if (game && with_memory) {
        HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, g_t->pid);
        if (h) {
            uint64_t off = g_t->mem_offset;
            snprintf(path, sizeof path, "%s\\progress_527B78.bin", dir);
            size_t a = dump_guest(h, off, HPCOS_PROGRESS_VA, HPCOS_PROGRESS_SIZE, path);
            snprintf(path, sizeof path, "%s\\guest_00000000.bin", dir);
            size_t b = dump_guest(h, off, 0x00000000u, 0x04000000u, path);
            snprintf(path, sizeof path, "%s\\guest_80000000.bin", dir);
            size_t c = dump_guest(h, off, 0x80000000u, 0x04000000u, path);
            report_line(f, "memory: progress block %zu bytes, guest 0x00000000 %zu MB, guest 0x80000000 %zu MB read",
                        a, b >> 20, c >> 20);
            CloseHandle(h);
        } else {
            report_line(f, "memory: could not open the game (error %lu)", GetLastError());
        }
    }

    char run_name[MAX_PATH], kernel_name[MAX_PATH];
    run_log("hpcos_run_", "hpcos_run.log", run_name, sizeof run_name);
    run_log("xbox_kernel_", "xbox_kernel.log", kernel_name, sizeof kernel_name);
    report_line(f, "logs: %s %s, %s %s, hpcos.toml %s",
                run_name, copy_in(run_name, dir, "hpcos_run.log") ? "copied" : "missing",
                kernel_name, copy_in(kernel_name, dir, "xbox_kernel.log") ? "copied" : "missing",
                copy_in("hpcos.toml", dir, "hpcos.toml") ? "copied" : "missing");

    if (game) {
        int answered = 0;
        for (int i = 0; i < 100 && !answered; i++, Sleep(50)) answered = g_t->report_done == asked;
        report_line(f, "game's part (stacks.txt, game_state.txt): %s",
                    answered ? "written" : "NO ANSWER in 5 s - the game's helper thread is stuck too");
        /* the capture comes with the next frame the game presents */
        char cap[MAX_PATH] = "";
        for (int i = 0; i < 40 && !newest_capture(t0, cap, sizeof cap); i++) Sleep(100);
        if (cap[0]) {
            Sleep(1500);  /* its textures and shaders are still being written */
            char rel[MAX_PATH];
            snprintf(rel, sizeof rel, "captures\\%s\\final.bmp", cap);
            copy_in(rel, dir, "screen.bmp");
            snprintf(rel, sizeof rel, "captures\\%s\\frame.txt", cap);
            copy_in(rel, dir, "frame.txt");
            report_line(f, "frame capture: run\\captures\\%s (screen.bmp, frame.txt copied)", cap);
        } else {
            report_line(f, "frame capture: none (the game presented no frame: paused in a load or hung)");
        }
    }
    fclose(f);
    snprintf(g_report_msg, sizeof g_report_msg, "Saved: run\\reports\\%s - tell Claude this name", stamp);
    PostMessageA(g_wnd, WM_REPORT_DONE, 0, 0);
    return 0;
}

static void start_report(void)
{
    if (InterlockedExchange(&g_reporting, 1)) return;
    EnableWindow(g_report_btn, FALSE);
    SetWindowTextA(g_report_status, "Collecting...");
    int mem = checked(g_report_mem);
    CloseHandle(CreateThread(NULL, 0, report_worker, (LPVOID)(INT_PTR)mem, 0, NULL));
}

/* ── window ───────────────────────────────────────────────────────────────── */

static void refresh(void)
{
    char b[256];
    if (!attach()) {
        SetWindowTextA(g_status, "Waiting for hpcos.exe...");
        return;
    }
    if (g_t->frame != g_last_frame) g_last_frame = g_t->frame, g_stale = 0;
    else g_stale++;
    if (!g_t->player) snprintf(b, sizeof b, "Connected - Harry not in play yet");
    else snprintf(b, sizeof b, "Connected - %s%s", g_stale > 5 ? "paused / loading" : "in play",
                  g_t->noclip_on ? " - NOCLIP" : "");
    SetWindowTextA(g_status, b);
    snprintf(b, sizeof b, "Position %.1f  %.1f  %.1f     Health %d / %d", g_t->pos[0], g_t->pos[1], g_t->pos[2],
             g_t->health, g_t->health_max);
    SetWindowTextA(g_pos, b);
    snprintf(b, sizeof b, "Cards %u / 101 (%u from trainer)   Beans %d / %d   Spells %d / 8", g_t->cards,
             g_t->cards_given, g_t->beans, g_t->beans_max, spells_known(g_t->spells));
    SetWindowTextA(g_collect, b);
    snprintf(b, sizeof b, "Now: %u  %s", g_t->level, g_t->player ? level_name(g_t->level) : "-");
    SetWindowTextA(g_level_now, b);
    EnableWindow(g_undo, g_t->undo_ready);
    /* the game restarted: send the settings again */
    if (g_t->noclip != (uint32_t)checked(g_noclip) || g_t->god != (uint32_t)checked(g_god) ||
        g_t->all_spells != (uint32_t)checked(g_spells))
        push_settings();
}

static HWND add(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

static int build(void)
{
    int y = 10;
    g_status = add("STATIC", "", 0, 10, y, 420, 18, ID_STATUS);
    y += 20;
    g_pos = add("STATIC", "", 0, 10, y, 420, 18, ID_POS);
    y += 28;
    add("BUTTON", "Movement", BS_GROUPBOX, 5, y, 430, 70, 0);
    g_noclip = add("BUTTON", "Noclip / fly  (Ctrl+Shift+N)", BS_AUTOCHECKBOX, 15, y + 20, 220, 20, ID_NOCLIP);
    add("STATIC", "Speed", 0, 250, y + 22, 40, 18, 0);
    g_speed = add("EDIT", "10", WS_BORDER, 295, y + 20, 60, 20, ID_SPEED);
    add("STATIC", "Left stick: move   RT/LT: up/down   A: fast", 0, 15, y + 45, 400, 18, 0);
    y += 78;
    g_god = add("BUTTON", "Infinite health  (Ctrl+Shift+G)", BS_AUTOCHECKBOX, 15, y, 210, 20, ID_GOD);
    g_spells = add("BUTTON", "Every spell", BS_AUTOCHECKBOX, 235, y, 190, 20, ID_SPELLS);
    y += 28;

    add("BUTTON", "Collection", BS_GROUPBOX, 5, y, 430, 188, 0);
    g_collect = add("STATIC", "", 0, 15, y + 20, 415, 18, ID_COLLECT);
    add("BUTTON", "Give all cards", BS_PUSHBUTTON, 15, y + 42, 130, 24, ID_CARDS);
    add("BUTTON", "Fill health", BS_PUSHBUTTON, 155, y + 42, 130, 24, ID_FILL);
    add("BUTTON", "Max health (255)", BS_PUSHBUTTON, 295, y + 42, 130, 24, ID_HEALTH_MAX);
    add("BUTTON", "Fill beans", BS_PUSHBUTTON, 15, y + 70, 130, 24, ID_BEANS);
    add("BUTTON", "Bigger bag (+50)", BS_PUSHBUTTON, 155, y + 70, 130, 24, ID_BAG);
    g_beans_value = add("EDIT", "0", WS_BORDER | ES_NUMBER, 295, y + 71, 50, 22, ID_BEANS_VALUE);
    add("BUTTON", "Set beans", BS_PUSHBUTTON, 350, y + 70, 75, 24, ID_BEANS_SET);
    /* card 101, Harry Potter's ("The boy who lived"), is Dumbledore's to give at the end */
    add("BUTTON", "All cards but Harry Potter's (100 of 101)", BS_PUSHBUTTON, 15, y + 98, 270, 24, ID_CARDS_NOT_HARRY);
    add("STATIC", "Take away:", 0, 15, y + 132, 100, 18, 0);
    add("BUTTON", "Trainer's cards", BS_PUSHBUTTON, 15, y + 152, 130, 24, ID_CARDS_RM);
    g_undo = add("BUTTON", "Undo all changes", BS_PUSHBUTTON, 155, y + 152, 130, 24, ID_UNDO);
    y += 196;

    add("BUTTON", "Level (the pause menu's level selector)", BS_GROUPBOX, 5, y, 430, 106, 0);
    g_level = add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL, 15, y + 20, 230, 300, ID_LEVEL);
    for (int i = 0; i < HPCOS_LEVELS; i++) {
        char l[64];
        snprintf(l, sizeof l, "%2d  %s", i, kLevels[i]);
        SendMessageA(g_level, CB_ADDSTRING, 0, (LPARAM)l);
    }
    SendMessageA(g_level, CB_SETCURSEL, 0, 0);
    add("STATIC", "Start", 0, 255, y + 23, 32, 18, 0);
    g_start = add("EDIT", "0", WS_BORDER | ES_NUMBER, 290, y + 20, 40, 22, ID_START);
    add("BUTTON", "Warp", BS_PUSHBUTTON, 345, y + 19, 80, 24, ID_WARP);
    g_replay = add("BUTTON", "Replay: forget the level's progress (Library, Chamber of Secrets)", BS_AUTOCHECKBOX,
                   15, y + 50, 410, 20, ID_REPLAY);
    g_level_now = add("STATIC", "", 0, 15, y + 76, 410, 18, ID_LEVEL_NOW);
    y += 114;

    add("BUTTON", "Saved positions", BS_GROUPBOX, 5, y, 430, 108, 0);
    for (int i = 0; i < 3; i++) {
        char l[32];
        int yy = y + 20 + i * 28;
        snprintf(l, sizeof l, "Save %d", i + 1);
        add("BUTTON", l, BS_PUSHBUTTON, 15, yy, 60, 24, ID_SAVE0 + i);
        snprintf(l, sizeof l, "Go %d", i + 1);
        add("BUTTON", l, BS_PUSHBUTTON, 80, yy, 60, 24, ID_GO0 + i);
        g_spot[i] = add("STATIC", "", 0, 150, yy + 4, 270, 18, ID_SPOT0 + i);
        show_spot(i);
    }
    y += 112;
    add("STATIC", "Ctrl+Shift+1..3 save, Ctrl+1..3 go (also from the game)", 0, 10, y, 420, 18, 0);
    y += 26;

    add("BUTTON", "Problem? Debug report for Claude", BS_GROUPBOX, 5, y, 430, 92, 0);
    g_report_btn = add("BUTTON", "Collect debug info  (Ctrl+Shift+R)", BS_PUSHBUTTON, 15, y + 20, 230, 26, ID_REPORT);
    g_report_mem = add("BUTTON", "With memory (128 MB)", BS_AUTOCHECKBOX, 255, y + 23, 170, 20, ID_REPORT_MEM);
    SendMessageA(g_report_mem, BM_SETCHECK, BST_CHECKED, 0);
    g_report_status = add("STATIC", "Stacks, game state, a frame capture, logs and memory into run\\reports",
                          0, 15, y + 54, 415, 32, ID_REPORT_STATUS);
    y += 100;
    return y;
}

static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_wnd = w;
        build();
        RegisterHotKey(w, HK_NOCLIP, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'N');
        RegisterHotKey(w, HK_GOD, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'G');
        RegisterHotKey(w, HK_REPORT, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'R');
        for (int i = 0; i < 3; i++) {
            RegisterHotKey(w, HK_SAVE0 + i, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, '1' + i);
            RegisterHotKey(w, HK_GO0 + i, MOD_CONTROL | MOD_NOREPEAT, '1' + i);
        }
        SetTimer(w, ID_TIMER, 100, NULL);
        return 0;
    case WM_TIMER:
        refresh();
        return 0;
    case WM_REPORT_DONE:
        SetWindowTextA(g_report_status, g_report_msg);
        EnableWindow(g_report_btn, TRUE);
        InterlockedExchange(&g_reporting, 0);
        return 0;
    case WM_HOTKEY:
        if (wp == HK_NOCLIP) toggle(g_noclip);
        else if (wp == HK_GOD) toggle(g_god);
        else if (wp == HK_REPORT) start_report();
        else if (wp >= HK_SAVE0 && wp < HK_SAVE0 + 3) save_spot((int)wp - HK_SAVE0);
        else if (wp >= HK_GO0 && wp < HK_GO0 + 3) go_spot((int)wp - HK_GO0);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_NOCLIP || id == ID_GOD || id == ID_SPELLS) push_settings();
        else if (id == ID_SPEED && code == EN_CHANGE) push_settings();
        else if (id == ID_REPORT) start_report();
        else if (id == ID_WARP) warp();
        else if (id >= ID_SAVE0 && id < ID_SAVE0 + 3) save_spot(id - ID_SAVE0);
        else if (id >= ID_GO0 && id < ID_GO0 + 3) go_spot(id - ID_GO0);
        else if (!attach()) return 0;
        else if (id == ID_CARDS) g_t->cards_skip = 0, g_t->cards_seq++;
        else if (id == ID_CARDS_NOT_HARRY) g_t->cards_skip = 101, g_t->cards_seq++;
        else if (id == ID_CARDS_RM) g_t->cards_remove_seq++;
        else if (id == ID_FILL) g_t->health_fill_seq++;
        else if (id == ID_HEALTH_MAX) g_t->health_max_seq++;
        else if (id == ID_BEANS) g_t->beans_seq++;
        else if (id == ID_BAG) g_t->bag_seq++;
        else if (id == ID_UNDO) {
            SendMessageA(g_spells, BM_SETCHECK, BST_UNCHECKED, 0);
            g_t->all_spells = 0;
            g_t->undo_seq++;
        } else if (id == ID_BEANS_SET) {
            char buf[16];
            GetWindowTextA(g_beans_value, buf, sizeof buf);
            g_t->beans_value = (int16_t)atoi(buf);
            g_t->beans_set_seq++;
        }
        return 0;
    }
    case WM_DESTROY:
        /* leave the game as it was */
        if (g_t) g_t->noclip = 0, g_t->god = 0, g_t->all_spells = 0;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev, (void)cmd;
    GetModuleFileNameA(NULL, g_dir, sizeof g_dir);
    char *slash = strrchr(g_dir, '\\');
    if (slash) *slash = 0;
    InitCommonControls();
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "hpcos_trainer";
    RegisterClassA(&wc);
    RECT r = {0, 0, 440, 802};  /* the controls are 28 px taller since the cards-but-Harry row */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    HWND w = CreateWindowExA(WS_EX_TOPMOST, "hpcos_trainer", "HP:CoS debug trainer", style, CW_USEDEFAULT,
                             CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(w, show);
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageA(w, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
