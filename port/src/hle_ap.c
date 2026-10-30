/**
 * The game side of the Archipelago bridge (src/ap/bridge.cpp): what in the title counts as a check, and giving the
 * items the multiworld sends. Everything here runs on the title's own thread: hpcos_ApFrame from Harry's update
 * (hle_trainer.c), the travel page's calls from the pause menu (hle_frontend.c). Without RECOMP_AP_SERVER /
 * RECOMP_AP_SLOT the bridge is off and nothing here acts.
 *
 * Checks are keyed as port/ap/catalog.json (make_catalog.py) names them, from counters the progress block keeps,
 * compared once a frame: wizard cards (a word per card at 0x527E0A + 2n), beans collected (rises of 0x527EE0), the
 * spell set's bits (0x527F0C), the level being played (0x527B78; a level entered the first time), Gryffindor's house
 * points (0x527EE4). The goal is the credits (level 52).
 *
 * Items: a card through sub_00081A10(0x2000 + card) (the title's give-item, as a pickup does), a spell by its bit in the
 * spell set, beans into 0x527EE0 up to the bag (0x527EE2), the bag 50 bigger, the health maximum 10 higher (0x527EF6,
 * at most 255) and a refill (sub_00081860), house points through sub_000818C0(points, 0 Gryffindor). Passes are kept
 * by the bridge (the travel page). What the multiworld gives is not counted as a check.
 * TODO: the vanilla reward of a check stays given (a card picked up is still the player's); a reward swap comes with
 * the pickup census.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"
#include "ap/bridge.h"

extern void sub_00081A10(void);
extern void sub_00081860(void);
extern void sub_000818C0(void);
extern void sub_0004AD50(void);
extern uint32_t hpcos_CallGuest(void (*fn)(void), int n, const uint32_t *args);  /* hle_trainer.c */

enum {
    kProgress = 0x527B78, kCards = 0x527E0A, kCardCount = 101, kBeans = 0x527EE0, kBeansMax = 0x527EE2,
    kHouse = 0x527EE4, kHealthMax = 0x527EF6, kSpells = 0x527F0C, kLevelStart = 0x527DFC, kSelectorStart = 0x1EF704,
    kCardItem = 0x2000, kLevels = 59, kCreditsLevel = 52,
};

static struct {
    int ready;
    uint8_t card[kCardCount + 1], visited[kLevels];
    uint32_t spells, ap_spells;
    int beans, beans_got, ap_beans, house;
    uint8_t ap_card[kCardCount + 1];
} g;

static uint32_t g_warp_level = 0xFFFFFFFFu;
static DWORD g_warp_seen;

static void check(const char *fmt, int n)
{
    char key[48];
    snprintf(key, sizeof key, fmt, n);
    hpcos_ap_location(key);
}

static void give(const char *kind, int value)
{
    if (!strcmp(kind, "card") && value >= 1 && value <= kCardCount) {
        if (!MEM16(kCards + 2 * value)) {
            g.ap_card[value] = 1;
            uint32_t a[1] = {kCardItem + (uint32_t)value};
            hpcos_CallGuest(sub_00081A10, 1, a);
        }
    } else if (!strcmp(kind, "spell") && value >= 0 && value < 32) {
        g.ap_spells |= 1u << value;
        MEM32(kSpells) |= 1u << value;
    } else if (!strcmp(kind, "beans")) {
        int cap = (int16_t)MEM16(kBeansMax), v = (int16_t)MEM16(kBeans) + value;
        if (v > cap) v = cap;
        g.ap_beans += v - (int16_t)MEM16(kBeans);
        MEM16(kBeans) = (uint16_t)v;
    } else if (!strcmp(kind, "bag")) {
        int v = (int16_t)MEM16(kBeansMax) + 50;
        MEM16(kBeansMax) = (uint16_t)(v > 32000 ? 32000 : v);
    } else if (!strcmp(kind, "health")) {
        int v = (int16_t)MEM16(kHealthMax) + 10;
        MEM16(kHealthMax) = (uint16_t)(v > 255 ? 255 : v);
        uint32_t a[1] = {MEM16(kHealthMax)};
        hpcos_CallGuest(sub_00081860, 1, a);
    } else if (!strcmp(kind, "heal")) {
        uint32_t a[1] = {MEM16(kHealthMax)};
        hpcos_CallGuest(sub_00081860, 1, a);
    } else if (!strcmp(kind, "house")) {
        uint32_t a[2] = {(uint32_t)value, 0};
        hpcos_CallGuest(sub_000818C0, 2, a);
    }
    fprintf(stderr, "[AP] gave %s %d\n", kind, value);
}

static void counters(void)
{
    int owned = 0;
    for (int c = 1; c <= kCardCount; c++) {
        int has = MEM16(kCards + 2 * c) != 0;
        owned += has;
        if (has && !g.card[c] && !g.ap_card[c]) check("card:%d", c);
        g.card[c] = (uint8_t)has;
    }
    for (int n = 5; n <= owned; n += 5) check("cards:%d", n);

    int beans = (int16_t)MEM16(kBeans);
    if (g.ready && beans > g.beans) g.beans_got += beans - g.beans;
    if (g.ap_beans) g.beans_got -= g.ap_beans, g.ap_beans = 0;
    g.beans = beans;
    static const int kBeanSteps[] = {10, 25};
    for (int i = 0; i < 2; i++)
        if (g.beans_got >= kBeanSteps[i]) check("beans:%d", kBeanSteps[i]);
    for (int n = 50; n <= g.beans_got && n <= 2000; n += 50) check("beans:%d", n);

    uint32_t spells = MEM32(kSpells), fresh = spells & ~g.spells & ~g.ap_spells;
    for (int b = 0; b < 32; b++)
        if (fresh & (1u << b)) check("spell:%d", b);
    g.spells = spells;

    uint32_t level = MEM32(kProgress);
    if (level < kLevels && !g.visited[level]) {
        g.visited[level] = 1;
        check("visit:%d", (int)level);
    }
    if (level == kCreditsLevel) hpcos_ap_goal();

    int house = (int16_t)MEM16(kHouse);
    for (int n = 25; n <= house && n <= 500; n += 25) check("house:%d", n);
    g.ready = 1;
}

/* the selector's start choice is read by a level's setup; put back to none once the warped-to level has run a while,
 * so later loads follow the story again (as the trainer's warp does) */
static void warp_settle(void)
{
    if (g_warp_level == 0xFFFFFFFFu || MEM32(kProgress) != g_warp_level) return;
    if (!g_warp_seen) g_warp_seen = GetTickCount();
    else if (GetTickCount() - g_warp_seen > 3000) {
        MEM32(kSelectorStart) = 0xFFFFFFFFu;
        g_warp_level = 0xFFFFFFFFu;
    }
}

void hpcos_ApFrame(void)
{
    if (!hpcos_ap_enabled()) return;
    hpcos_ap_frame();
    counters();
    const char *kind;
    int value;
    while (hpcos_ap_next_item(&kind, &value)) {
        give(kind, value);
        hpcos_ap_item_given();
    }
    warp_settle();
}

/* the travel page (hle_frontend.c): 0 rows without a bound slot */
int hpcos_ApTravelCount(void)
{
    if (!hpcos_ap_enabled() || !hpcos_ap_active()) return 0;
    const char *name;
    int level, start, unlocked, n = 0;
    while (hpcos_ap_travel_entry(n, &name, &level, &start, &unlocked)) n++;
    return n + 1;  /* The Burrow first: where a game starts, no pass */
}

/* a row of the page: 0 The Burrow, then the bridge's list */
static int travel_entry(int i, const char **name, int *level, int *start, int *unlocked)
{
    if (i == 0) {
        *name = "The Burrow", *level = 0, *start = -1, *unlocked = 1;
        return 1;
    }
    return hpcos_ap_travel_entry(i - 1, name, level, start, unlocked);
}

const char *hpcos_ApTravelRow(int i, int *unlocked)
{
    const char *name;
    int level, start;
    if (!travel_entry(i, &name, &level, &start, unlocked)) return "";
    return name;
}

/* the pause menu's level selector's way (0x4F268): the level, and the start it chose */
int hpcos_ApTravelGo(int i)
{
    const char *name;
    int level, start, unlocked;
    if (!travel_entry(i, &name, &level, &start, &unlocked) || !unlocked) return 0;
    if (start < 0) start = 0;  /* a level's first start */
    uint32_t a[1] = {(uint32_t)level};
    hpcos_CallGuest(sub_0004AD50, 1, a);
    MEM16(kLevelStart) = (uint16_t)start;
    MEM32(kSelectorStart) = (uint32_t)start;
    g_warp_level = (uint32_t)level, g_warp_seen = 0;
    fprintf(stderr, "[AP] travel to %s (level %d, start %d)\n", name, level, start);
    return 1;
}
