/**
 * The GameCube's Game Boy Advance bonus: the Gringotts portrait in the Entrance Hall.
 *
 * On the GameCube, linking a GBA with the handheld game (and finding its five location wizard cards) opened the
 * portrait, a door into the Gringotts level (gringott.geo, level index 6, door tag 0x10000042). The Xbox ships the
 * level, the portrait and the way back, but the door setup sub_0007C5B0 switches any door to Gringotts off
 * (sub_00042D60(door, 1)) and returns before it builds the door's trigger. RECOMP_GRINGOTTS=0 keeps that.
 *
 * The rest of sub_0007C5B0 reads the tag only to compare it with 0, 0x1000010B and 0x1000010C, so the door gets a
 * neutral tag for the call and 0x10000042 back afterwards, in its data and in the trigger the call made.
 *
 * Built into the recompile with --exclude-manual ../hpcos/src/hle_gringotts.c; gen/recomp_0002.c's generated body
 * is renamed sub_0007C5B0_gen by hand until then.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"

#define TAG_GRINGOTTS 0x10000042u
#define TAG_NEUTRAL   0x10000043u  /* any tag the call does not single out */

extern void sub_0007C5B0_gen(void);

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_GRINGOTTS");
        on = !(e && *e == '0');
    }
    return on;
}

void sub_0007C5B0(void)
{
    uint32_t obj = MEM32(esp + 4);
    uint32_t door = obj ? MEM32(obj + 0xA8) : 0;
    if (!enabled() || !door || MEM32(door + 0x14) != TAG_GRINGOTTS) {
        sub_0007C5B0_gen();
        return;
    }
    MEM32(door + 0x14) = TAG_NEUTRAL;
    sub_0007C5B0_gen();
    MEM32(door + 0x14) = TAG_GRINGOTTS;
    uint32_t trigger = MEM32(obj + 0xC0);
    if (trigger && MEM32(trigger + 0x14) == TAG_NEUTRAL) MEM32(trigger + 0x14) = TAG_GRINGOTTS;
    fprintf(stderr, "[GRINGOTTS] portrait door opened (object 0x%08X, trigger 0x%08X)\n", obj, trigger);
}
