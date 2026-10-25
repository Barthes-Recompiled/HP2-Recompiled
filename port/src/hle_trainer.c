/**
 * The debug trainer's game side (the trainer itself is hpcos_trainer.exe,
 * src/trainer): a named shared block (trainer_shm.h) and Harry's per-frame
 * update, sub_00053710(entity), as the place the requests are carried out --
 * the title's own thread, between its frames.
 *
 * What is known of the title (found for this):
 *   [0x530408]           Harry's entity; +0xAC his object
 *   object +0x14/18/1C   world position (y up), +0x3C yaw; the title's own
 *                        movers (the push code at 0x7DA7B) write them directly
 *   0x527B78             the progress block (0x2064 bytes, what a save holds);
 *                        its first dword is the level table index played
 *   0x527EF4 / 0x527EF6  health / its maximum (words, the maximum up to 255);
 *                        sub_00081860(n) heals, sub_00081890(n) raises the
 *                        maximum
 *   0x527F0C             the spells learnt (bits, HPCOS_ALL_SPELLS)
 *   0x527E0A + 2n        wizard card n (1..101), 0x527ED6 how many are held;
 *                        sub_00081A10(0x2000 + n) gives one (the pickup's
 *                        way: every 10th new card raises the maximum health)
 *   0x527EE0 / 0x527EE2  beans / the bag's capacity (50, +50 an upgrade)
 *   0x527B94 / 0x527DFC  the level to load next / its start point: the pause
 *                        menu's level selector (0x4F269) writes both
 *   0x51E094 / 0x51E098  the left stick after the title's deadzone;
 *                        sub_00036E10(x, y) is its angle, sub_00021BC0 adds
 *                        the camera's yaw -- the heading Harry runs along
 *
 * Noclip: Harry's update is skipped (no walking, falling, collision or
 * casting) and the left stick moves him along the heading the title would
 * give him, the right / left trigger up / down, A held 4x as fast.
 *
 * Undo: the first change the trainer makes keeps what it touches (cards,
 * health, beans, the bag, the spells) to put back.
 *
 * The debug report runs on a thread of its own (a hung title still answers):
 * every thread's native stack, the state below and a frame capture, into
 * the folder the trainer names; the trainer copies memory and logs itself.
 *
 * Built into the recompile with --exclude-manual ../hpcos/src/hle_trainer.c;
 * gen/recomp_0001.c's generated body is renamed sub_00053710_gen by hand
 * until then.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"
#include "input/xinput_xbox.h"
#include "trainer_shm.h"

#define ARG(i) MEM32(g_esp + 4 + 4 * (i))

enum {
    kPlayer = 0x530408, kProgress = 0x527B78, kHealth = 0x527EF4, kHealthMax = 0x527EF6, kHealthCap = 255,
    kSpells = 0x527F0C, kCards = 0x527E0A, kCardCount = 101, kCardTotal = 0x527ED6, kBeans = 0x527EE0,
    kBeansMax = 0x527EE2, kBagStep = 50, kLevelReq = 0x527B94, kLevelStart = 0x527DFC, kSelectorStart = 0x1EF704, kStickX = 0x51E094,
    kStickY = 0x51E098, kCardItem = 0x2000,
};

/* what the first trainer change found, to put back (Undo) */
static struct {
    int kept;
    uint16_t cards[kCardCount + 1], card_total;
    int16_t health, health_max, beans, beans_max;
    uint32_t spells;
} g_undo;
static uint8_t g_given[kCardCount + 1];  /* cards the trainer gave */

extern void sub_00053710_gen(void);
extern void sub_00081A10(void);
extern void sub_00041A60(void);
extern void sub_00081860(void);
extern void sub_00081890(void);
extern void sub_0004AD50(void);
extern void sub_00036E10(void);
extern void sub_00021BC0(void);
extern void hpcos_StackDumpTo(const char *path);
extern void hpcos_RequestCapture(void);
extern void hpcos_WideAspectTick(void); /* hle_video.c: 16:9 put back after a 4:3 screen */
extern ptrdiff_t g_xbox_mem_offset;

/* hle_input.c: the pad as the game last read it */
extern XBOX_GAMEPAD hpcos_last_pad;

static HpcosTrainerShm *g_shm;

/* A level's setup (message 0x20000003) sets its progress word from the selector's start only while the word is 0,
 * the level never played, or only some of its bits; on a save past it the start just places Harry and the level
 * stays done (the Library's Four Bolt Door duel, start 7, never comes back). Replay clears the word so the start sets it as on a new save.
 * The words, from each setup's check before its start switch: */
static const struct { uint16_t level; uint32_t addr; } kLevelProgress[] = {
    {2, 0x527CD8},  /* Library, sub_000EBA50: start 0..8 = 0 1 7 1F 3F 3F 17F 37F 77F */
    /* Chamber of Secrets, sub_000D5DF0: its starts always apply but keep the word's high bits (the fight's outcome);
     * cleared, start 2 At Chamber is the Basilisk fight as on a new save */
    {25, 0x527D20},
};

static void forget_level(uint32_t level)
{
    for (size_t i = 0; i < sizeof kLevelProgress / sizeof kLevelProgress[0]; i++)
        if (kLevelProgress[i].level == level) {
            fprintf(stderr, "[TRAINER] level %u progress %08X forgotten\n", level, MEM32(kLevelProgress[i].addr));
            MEM32(kLevelProgress[i].addr) = 0;
            return;
        }
    fprintf(stderr, "[TRAINER] level %u: where it keeps its progress isn't known, warping without replay\n", level);
}

static HpcosTrainerShm *shm(void);

/* hle_input.c: the trainer's injected pad state, added to port 0's while it lasts (inject_seq / inject_ms) */
void hpcos_TrainerInject(XBOX_GAMEPAD *g)
{
    static uint32_t seen;
    static DWORD until;
    HpcosTrainerShm *t = shm();
    if (!t) return;
    if (t->inject_seq != seen) {
        seen = t->inject_seq;
        until = GetTickCount() + t->inject_ms;
        fprintf(stderr, "[TRAINER] input %04X analog %02X%02X%02X%02X%02X%02X%02X%02X for %u ms\n", t->inject_buttons,
                t->inject_analog[0], t->inject_analog[1], t->inject_analog[2], t->inject_analog[3], t->inject_analog[4],
                t->inject_analog[5], t->inject_analog[6], t->inject_analog[7], t->inject_ms);
    }
    if ((LONG)(GetTickCount() - until) >= 0) return;
    g->wButtons |= t->inject_buttons;
    for (int i = 0; i < 8; i++)
        if (t->inject_analog[i] > g->bAnalogButtons[i]) g->bAnalogButtons[i] = t->inject_analog[i];
    if (t->inject_stick[0] || t->inject_stick[1]) g->sThumbLX = t->inject_stick[0], g->sThumbLY = t->inject_stick[1];
    if (t->inject_stick[2] || t->inject_stick[3]) g->sThumbRX = t->inject_stick[2], g->sThumbRY = t->inject_stick[3];
}

static HpcosTrainerShm *shm(void)
{
    static int tried;
    if (!g_shm && !tried) {
        tried = 1;
        HANDLE m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(HpcosTrainerShm),
                                      HPCOS_TRAINER_NAME);
        if (m) g_shm = (HpcosTrainerShm *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HpcosTrainerShm));
        if (g_shm) {
            memset(g_shm, 0, sizeof(*g_shm));
            g_shm->version = HPCOS_TRAINER_VERSION;
            g_shm->pid = GetCurrentProcessId();
            g_shm->mem_offset = (uint64_t)g_xbox_mem_offset;
            g_shm->magic = HPCOS_TRAINER_MAGIC;
        }
    }
    return g_shm;
}

/* runs a recompiled function on this guest thread, the arguments pushed right to left and a return address,
 * as `call` leaves them (hle_d3d8.cpp's CallGuest) */
static uint32_t call_guest(void (*fn)(void), int n, const uint32_t *args)
{
    uint32_t s_esp = g_esp, s_ebx = g_ebx, s_esi = g_esi, s_edi = g_edi, s_ebp = g_ebp;
    uint32_t sp = s_esp;
    for (int i = n; i-- > 0;) {
        sp -= 4;
        MEM32(sp) = args[i];
    }
    sp -= 4;
    MEM32(sp) = 0;  /* return address */
    g_esp = sp;
    fn();
    g_esp = s_esp, g_ebx = s_ebx, g_esi = s_esi, g_edi = s_edi, g_ebp = s_ebp;
    return g_eax;
}

/* the same for a function returning in st(0) */
static double call_guest_fp(void (*fn)(void), int n, const uint32_t *args)
{
    call_guest(fn, n, args);
    double r = g_fp_stack[g_fp_top & 7];
    g_fp_top = (g_fp_top + 1) & 7;
    return r;
}

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void place(uint32_t obj, const float p[3])
{
    MEMF(obj + 0x14) = p[0], MEMF(obj + 0x18) = p[1], MEMF(obj + 0x1C) = p[2];
}

static void fly(uint32_t obj, HpcosTrainerShm *t)
{
    static LARGE_INTEGER last, freq;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    float dt = last.QuadPart ? (float)((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart) : 0;
    last = now;
    if (!(dt > 0 && dt < 0.25f)) dt = 1 / 60.0f;

    float x = MEMF(kStickX), y = MEMF(kStickY);
    float m = sqrtf(x * x + y * y);
    if (m > 1) m = 1;
    XBOX_GAMEPAD g = hpcos_last_pad;
    float up = (g.bAnalogButtons[7] - g.bAnalogButtons[6]) / 255.0f;
    if (m < 0.05f && up == 0) return;
    float speed = t->fly_speed > 0 ? t->fly_speed : 10.0f;
    if (g.bAnalogButtons[0] > 64) speed *= 4;
    float k = speed * dt;
    float p[3] = {MEMF(obj + 0x14), MEMF(obj + 0x18), MEMF(obj + 0x1C)};
    if (m >= 0.05f) {
        /* the heading the title turns Harry to for this stick (0x62690) */
        uint32_t a[2] = {fbits(x), fbits(y)};
        uint32_t s[1] = {fbits((float)call_guest_fp(sub_00036E10, 2, a))};
        float h = (float)call_guest_fp(sub_00021BC0, 1, s);
        /* forward along the yaw is (sin, cos) in x / z: Harry's own step back (0x563E0) subtracts those */
        MEMF(obj + 0x3C) = h;
        p[0] += sinf(h) * m * k;
        p[2] += cosf(h) * m * k;
    }
    p[1] += up * k;
    place(obj, p);
}

static void keep_for_undo(void)
{
    if (g_undo.kept) return;
    g_undo.kept = 1;
    for (int c = 1; c <= kCardCount; c++) g_undo.cards[c] = MEM16(kCards + 2 * c);
    g_undo.card_total = MEM16(kCardTotal);
    g_undo.health = (int16_t)MEM16(kHealth), g_undo.health_max = (int16_t)MEM16(kHealthMax);
    g_undo.beans = (int16_t)MEM16(kBeans), g_undo.beans_max = (int16_t)MEM16(kBeansMax);
    g_undo.spells = MEM32(kSpells);
}

static void undo(HpcosTrainerShm *t)
{
    if (!g_undo.kept) return;
    for (int c = 1; c <= kCardCount; c++) MEM16(kCards + 2 * c) = g_undo.cards[c];
    MEM16(kCardTotal) = g_undo.card_total;
    MEM16(kHealthMax) = (uint16_t)g_undo.health_max;
    MEM16(kHealth) = (uint16_t)(g_undo.health < g_undo.health_max ? g_undo.health : g_undo.health_max);
    MEM16(kBeansMax) = (uint16_t)g_undo.beans_max, MEM16(kBeans) = (uint16_t)g_undo.beans;
    MEM32(kSpells) = g_undo.spells;
    memset(g_given, 0, sizeof g_given);
    g_undo.kept = 0;
    t->all_spells = 0;  /* else the next frame sets them again */
    fprintf(stderr, "[TRAINER] undone: back to before the first trainer change\n");
}

static int cards_held(void)
{
    int n = 0;
    for (int c = 1; c <= kCardCount; c++) n += MEM16(kCards + 2 * c) != 0;
    return n;
}

static void requests(HpcosTrainerShm *t, uint32_t obj)
{
    static uint32_t tp_done, cards_done, cards_rm_done, fill_done, max_done, beans_done, bag_done, beans_set_done,
        undo_done, warp_done;
    if (t->cards_seq != cards_done) {
        cards_done = t->cards_seq;
        keep_for_undo();
        int n = 0;
        for (uint32_t c = 1; c <= kCardCount; c++) {
            if (MEM16(kCards + 2 * c) || c == t->cards_skip) continue;
            uint32_t a[1] = {kCardItem + c};
            call_guest(sub_00081A10, 1, a);
            g_given[c] = 1;
            n++;
        }
        fprintf(stderr, "[TRAINER] %d wizard cards given%s, health max %d\n", n,
                t->cards_skip ? " (Harry Potter's left out)" : "", (int16_t)MEM16(kHealthMax));
    }
    if (t->cards_remove_seq != cards_rm_done) {
        cards_rm_done = t->cards_remove_seq;
        int n = 0;
        for (int c = 1; c <= kCardCount; c++)
            if (g_given[c]) {
                if (MEM16(kCards + 2 * c)) {
                    MEM16(kCards + 2 * c) = 0;
                    n++;
                }
                g_given[c] = 0;
            }
        MEM16(kCardTotal) = (uint16_t)(MEM16(kCardTotal) > n ? MEM16(kCardTotal) - n : 0);
        fprintf(stderr, "[TRAINER] %d given cards taken back\n", n);
    }
    if (t->health_fill_seq != fill_done) {
        fill_done = t->health_fill_seq;
        keep_for_undo();
        uint32_t a[1] = {(uint32_t)(int16_t)MEM16(kHealthMax)};
        call_guest(sub_00081860, 1, a);
    }
    if (t->health_max_seq != max_done) {
        max_done = t->health_max_seq;
        keep_for_undo();
        uint32_t a[1] = {kHealthCap};
        call_guest(sub_00081890, 1, a);
        call_guest(sub_00081860, 1, a);
    }
    if (t->bag_seq != bag_done) {
        bag_done = t->bag_seq;
        keep_for_undo();
        int v = (int16_t)MEM16(kBeansMax) + kBagStep;
        if (v > 32000) v = 32000;
        MEM16(kBeansMax) = (uint16_t)v;
    }
    if (t->beans_seq != beans_done) {
        beans_done = t->beans_seq;
        keep_for_undo();
        MEM16(kBeans) = MEM16(kBeansMax);
    }
    if (t->beans_set_seq != beans_set_done) {
        beans_set_done = t->beans_set_seq;
        keep_for_undo();
        int v = t->beans_value, cap = (int16_t)MEM16(kBeansMax);
        MEM16(kBeans) = (uint16_t)(v < 0 ? 0 : v > cap ? cap : v);
    }
    if (t->undo_seq != undo_done) {
        undo_done = t->undo_seq;
        undo(t);
    }
    if (t->tp_seq != tp_done) {
        tp_done = t->tp_seq;
        float p[3] = {t->tp_pos[0], t->tp_pos[1], t->tp_pos[2]};
        if (obj) place(obj, p);
    }
    /* the selector's start choice ([0x1EF704], -1 none) is read by a level's setup (0x20000003, e.g. sub_000D5DF0:
     * Chamber of Secrets 0 Start .. 4 PolyJuice .. 7 Default); put back to none once the warped-to level has run a
     * while, so later loads follow the story again */
    static uint32_t warp_level = 0xFFFFFFFFu;
    static DWORD warp_seen;
    if (warp_level != 0xFFFFFFFFu && MEM32(0x527B78) == warp_level) {
        if (!warp_seen) warp_seen = GetTickCount();
        else if (GetTickCount() - warp_seen > 3000) {
            MEM32(kSelectorStart) = 0xFFFFFFFFu;
            warp_level = 0xFFFFFFFFu;
        }
    }
    if (t->warp_seq != warp_done) {
        warp_done = t->warp_seq;
        if (t->warp_level < HPCOS_LEVELS) {
            /* the pause menu's level selector's way (0x4F268): the level, and the start it chose */
            uint32_t a[1] = {t->warp_level};
            if (t->warp_replay) forget_level(t->warp_level);
            call_guest(sub_0004AD50, 1, a);
            MEM16(kLevelStart) = t->warp_start;
            MEM32(kSelectorStart) = t->warp_start;
            warp_level = t->warp_level, warp_seen = 0;
            fprintf(stderr, "[TRAINER] warp to level %u, start %u\n", t->warp_level, t->warp_start);
        }
    }
}

/* debug: RECOMP_GIVE_CARDS=65,46,... -- Ctrl+Shift+C asks for the next card of the list's call-out the way its pop-up
 * does (sub_000A0BE0: sub_00041A60(field 5 of the card's entry in 0x1F4218, 1.0)), from Harry's update; for testing
 * the call-outs (hle_debug.c's RECOMP_SND_TRACE). Giving the card itself (sub_00081A10) opens no pop-up */
static void give_card_key(uint32_t ent)
{
    static int cards[32], n = -1, next;
    static int was;
    if (n < 0) {
        n = 0;
        const char *e = getenv("RECOMP_GIVE_CARDS");
        while (e && *e && n < 32) {
            int c = atoi(e);
            if (c >= 1 && c <= kCardCount) cards[n++] = c;
            e = strchr(e, ',');
            if (e) e++;
        }
    }
    if (!n || ent != MEM32(kPlayer)) return;
    int down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
               (GetAsyncKeyState('C') & 0x8000);
    if (down && !was) {
        int c = cards[next++ % n];
        uint32_t a[2] = {MEM32(0x1F4218u + 20u * (uint32_t)(c - 1) + 0x10u), 0x3F800000u};
        fprintf(stderr, "[TRAINER] wizard card %d's call-out, sound %08X\n", c, a[0]);
        call_guest(sub_00041A60, 2, a);
    }
    was = down;
}

/* debug, RECOMP_HARRY_DIFF=1: [HDIFF] lines after a press of A, B, X, Y or a trigger: for 90 updates (1.5 s) every
 * dword of Harry's entity (0x400 bytes) and object (0x200) that changed since the update before, and the game's data
 * (0x1D0000 to 0x220000 and 0x500000 to 0x560000) against the update before the press 15 and 60 updates on
 * ([HDIFF-G]). What changes while no button is held (every 30 updates compared) is noise and left out of those (Harry's
 * bones, timers): to find the state a spell's cast keeps (2026-10-25: the L trigger during Alohomora moves the spell) */
static const struct { uint32_t va, n; } kHdRanges[] = {{0x1D0000u, 0x50000u / 4}, {0x500000u, 0x60000u / 4}};
#define HD_N (0x50000u / 4 + 0x60000u / 4)
static uint32_t hd_va(uint32_t k)
{
    return k < kHdRanges[0].n ? kHdRanges[0].va + 4 * k : kHdRanges[1].va + 4 * (k - kHdRanges[0].n);
}

static void harry_diff(uint32_t ent)
{
    static int on = -1, left, step, quiet;
    static uint32_t prev[0x180], held, ent0, *glo, *base;
    static uint8_t *noisy;
    static const char *const kNames[] = {"A", "B", "X", "Y", "Black", "White", "LT", "RT"};
    static int which;
    if (on < 0) {
        const char *e = getenv("RECOMP_HARRY_DIFF");
        on = e && e[0] == '1';
        if (on) glo = (uint32_t *)malloc(HD_N * 4), base = (uint32_t *)malloc(HD_N * 4), noisy = (uint8_t *)calloc(HD_N, 1);
        if (on && base) for (uint32_t k = 0; k < HD_N; k++) base[k] = MEM32(hd_va(k));
    }
    if (!on || !glo || !base || !noisy) return;
    uint32_t obj = MEM32(ent + 0xAC), down = 0;
    XBOX_GAMEPAD g = hpcos_last_pad;
    for (int i = 0; i < 8; i++)
        if (g.bAnalogButtons[i] > 30) down |= 1u << i;
    uint32_t pressed = down & ~held;
    held = down;
    /* the noise: changes over 30 updates with nothing held and no trace running */
    if (down || left) quiet = 0;
    else if (++quiet % 30 == 0) {
        for (uint32_t k = 0; k < HD_N; k++) {
            uint32_t v = MEM32(hd_va(k));
            if (v != base[k]) noisy[k] = 1, base[k] = v;
        }
    }
    static char line[16384];
    if (left) {
        step++;
        int n = snprintf(line, sizeof line, "[HDIFF] %s +%d:", kNames[which], step), any = 0;
        for (uint32_t k = 0; k < 0x180 && n < (int)sizeof line - 40; k++) {
            uint32_t v = MEM32(k < 0x100 ? ent0 + 4 * k : obj + 4 * (k - 0x100));
            if (v != prev[k]) {
                n += snprintf(line + n, sizeof line - n, " %s+%X %X->%X", k < 0x100 ? "e" : "o",
                              k < 0x100 ? 4 * k : 4 * (k - 0x100), prev[k], v);
                prev[k] = v, any = 1;
            }
        }
        if (any) fprintf(stderr, "%s\n", line);
        if (step == 15 || step == 60) {
            n = snprintf(line, sizeof line, "[HDIFF-G] %s +%d:", kNames[which], step);
            int c = 0;
            for (uint32_t k = 0; k < HD_N && c < 300; k++) {
                uint32_t v = MEM32(hd_va(k));
                if (v != glo[k] && !noisy[k]) n += snprintf(line + n, sizeof line - n, " %X:%X->%X", hd_va(k), glo[k], v), c++;
            }
            fprintf(stderr, "%s\n", line);
        }
        if (--left == 0) {
            fprintf(stderr, "[HDIFF] %s end\n", kNames[which]);
            for (uint32_t k = 0; k < HD_N; k++) base[k] = MEM32(hd_va(k));
        }
    }
    if (pressed && !left) {
        for (which = 0; !(pressed >> which & 1); which++) {}
        ent0 = ent, step = 0, left = 90;
        for (uint32_t k = 0; k < 0x180; k++) prev[k] = MEM32(k < 0x100 ? ent + 4 * k : obj + 4 * (k - 0x100));
        for (uint32_t k = 0; k < HD_N; k++) glo[k] = MEM32(hd_va(k));
        uint32_t nn = 0;
        for (uint32_t k = 0; k < HD_N; k++) nn += noisy[k];
        fprintf(stderr, "[HDIFF] %s pressed (%u noisy dwords left out)\n", kNames[which], nn);
    }
}

void sub_00053710(void)
{
    HpcosTrainerShm *t = shm();
    uint32_t ent = ARG(0);
    give_card_key(ent);
    if (ent && ent == MEM32(kPlayer)) hpcos_WideAspectTick(), harry_diff(ent);
    if (!t || !ent || ent != MEM32(kPlayer)) {
        sub_00053710_gen();
        return;
    }
    static uint32_t seen;
    if (seen != ent) {
        seen = ent;
        fprintf(stderr, "[TRAINER] Harry's update: entity %08X object %08X\n", ent, MEM32(ent + 0xAC));
    }
    uint32_t obj = MEM32(ent + 0xAC);

    if (t->god && (int16_t)MEM16(kHealthMax) > 0) MEM16(kHealth) = MEM16(kHealthMax);
    if (t->all_spells && (MEM32(kSpells) & HPCOS_ALL_SPELLS) != HPCOS_ALL_SPELLS) {
        keep_for_undo();
        MEM32(kSpells) |= HPCOS_ALL_SPELLS;
    }
    requests(t, obj);

    int noclip = t->noclip && obj;
    if (noclip) {
        fly(obj, t);
        g_esp += 4;  /* the update's ret */
    } else {
        sub_00053710_gen();
    }

    t->noclip_on = (uint8_t)noclip;
    t->player = ent;
    if (obj) {
        t->pos[0] = MEMF(obj + 0x14), t->pos[1] = MEMF(obj + 0x18), t->pos[2] = MEMF(obj + 0x1C);
        t->yaw = MEMF(obj + 0x3C);
    }
    t->health = (int16_t)MEM16(kHealth), t->health_max = (int16_t)MEM16(kHealthMax);
    t->spells = MEM32(kSpells);
    t->cards = (uint16_t)cards_held();
    uint16_t given = 0;
    for (int c = 1; c <= kCardCount; c++) given += g_given[c];
    t->cards_given = given;
    t->undo_ready = (uint8_t)g_undo.kept;
    t->beans = (int16_t)MEM16(kBeans), t->beans_max = (int16_t)MEM16(kBeansMax);
    t->level = (uint16_t)MEM32(kProgress);
    t->frame++;
}

static void write_state(const char *path, HpcosTrainerShm *t)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "time %04u-%02u-%02u %02u:%02u:%02u, %.1f s since boot\n", st.wYear, st.wMonth, st.wDay, st.wHour,
            st.wMinute, st.wSecond, GetTickCount64() / 1000.0);
    fprintf(f, "guest -> host offset %+lld\n", (long long)g_xbox_mem_offset);
    fprintf(f, "Harry's updates %u (last player %08X)%s\n", t->frame, t->player, t->noclip_on ? ", noclip" : "");
    uint32_t pl = MEM32(kPlayer);
    fprintf(f, "player entity [0x530408] %08X", pl);
    if (pl) {
        uint32_t obj = MEM32(pl + 0xAC);
        fprintf(f, " object %08X", obj);
        if (obj)
            fprintf(f, " position %.3f %.3f %.3f yaw %.3f", MEMF(obj + 0x14), MEMF(obj + 0x18), MEMF(obj + 0x1C),
                    MEMF(obj + 0x3C));
    }
    fprintf(f, "\nlevel index [0x527B78] %u, next level [0x527B94] %d start [0x527DFC] %d\n", MEM32(kProgress),
            (int16_t)MEM16(kLevelReq), (int16_t)MEM16(kLevelStart));
    fprintf(f, "player state [0x530298] %d flags [0x5302A0] %08X [0x5302A4] %08X\n", (int16_t)MEM16(0x530298),
            MEM32(0x5302A0), MEM32(0x5302A4));
    fprintf(f, "health %d / %d, beans %d / %d, cards %d (total word %u, trainer gave %u), spells %08X\n",
            (int16_t)MEM16(kHealth), (int16_t)MEM16(kHealthMax), (int16_t)MEM16(kBeans), (int16_t)MEM16(kBeansMax),
            cards_held(), MEM16(kCardTotal), t->cards_given, MEM32(kSpells));
    fprintf(f, "stick after deadzone %.3f %.3f\n", MEMF(kStickX), MEMF(kStickY));
    XBOX_GAMEPAD g = hpcos_last_pad;
    fprintf(f, "pad buttons %04X analog", g.wButtons);
    for (int i = 0; i < 8; i++) fprintf(f, " %u", g.bAnalogButtons[i]);
    fprintf(f, " sticks %d %d %d %d\n", g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY);
    fprintf(f, "trainer: noclip %u god %u all spells %u undo kept %d\n", t->noclip, t->god, t->all_spells,
            g_undo.kept);
    fclose(f);
}

static DWORD WINAPI report_thread(LPVOID arg)
{
    HpcosTrainerShm *t = (HpcosTrainerShm *)arg;
    for (;; Sleep(100)) {
        uint32_t seq = t->report_seq;
        if (seq == t->report_done) continue;
        char dir[260], path[300];
        memcpy(dir, (const char *)t->report_dir, sizeof dir);
        dir[sizeof dir - 1] = 0;
        hpcos_RequestCapture();
        snprintf(path, sizeof path, "%s\\stacks.txt", dir);
        hpcos_StackDumpTo(path);
        snprintf(path, sizeof path, "%s\\game_state.txt", dir);
        write_state(path, t);
        fprintf(stderr, "[TRAINER] debug report written to %s\n", dir);
        t->report_done = seq;
    }
    return 0;
}

/* at boot, so the trainer finds the block before Harry exists */
void hpcos_TrainerInit(void)
{
    HpcosTrainerShm *t = shm();
    if (!t) return;
    fprintf(stderr, "[TRAINER] shared block %s ready\n", HPCOS_TRAINER_NAME);
    CloseHandle(CreateThread(NULL, 0, report_thread, t, 0, NULL));
}
