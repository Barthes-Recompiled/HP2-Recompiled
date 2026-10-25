/**
 * Fading objects drawn after the solid ones (sludge cleared with Skurge, curtains cut with Diffindo).
 *
 * sub_00048A10(list, pass) walks a draw list twice: pass 0 draws the objects without flag 0x10 in their flags
 * (object+0xE8), pass 1 those with it, after all the solid geometry. The title sets 0x10 for models it knows are
 * see-through (sub_00031E20, model flag 0x2000), but an object fading out keeps its pass and only lowers the alpha of
 * its colour (object+0x117, 0xFF when solid): it is drawn among the solid pieces, before the room behind it, so it
 * blends with the empty frame (the level's fog colour) and its depth then hides what comes later (a door gone blue
 * behind a dissolving sludge). The Xbox does the same; here an object with alpha below 0xFF counts as flagged 0x10
 * for the walk and gets its old flags back afterwards. RECOMP_FADE_LATE=0 keeps the Xbox's order.
 *
 * Built into the recompile with --exclude-manual ../hpcos/src/hle_fade.c; gen/recomp_0001.c's generated body is
 * renamed sub_00048A10_gen by hand until then.
 */

#include <stdint.h>
#include <stdlib.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"

#define FLAG_SEE_THROUGH 0x10u
#define LEVEL_INDEX      0x00527B78u  /* the progress block's first word */
#define LEVEL_CHAMBER    25u

extern void sub_00048A10_gen(void);

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_FADE_LATE");
        on = !(e && *e == '0');
    }
    return on;
}

/* __thiscall: ecx = the list (nodes from +0x10: node+0 the object, node+8 the next); the pass on the stack */
void sub_00048A10(void)
{
    uint32_t list = g_ecx;
    /* the Chamber of Secrets (level 25) keeps the Xbox's order: the Basilisk's venom glow and Ginny drew wrong with it */
    if (!enabled() || !list || MEM32(LEVEL_INDEX) == LEVEL_CHAMBER) {
        sub_00048A10_gen();
        return;
    }
    enum { kMax = 64 };
    uint32_t marked[kMax];
    int n = 0;
    for (uint32_t node = MEM32(list + 0x10); node && n < kMax; node = MEM32(node + 8)) {
        uint32_t obj = MEM32(node);
        if (obj && MEM8(obj + 0x117) != 0xFF && !(MEM32(obj + 0xE8) & FLAG_SEE_THROUGH)) {
            MEM32(obj + 0xE8) |= FLAG_SEE_THROUGH;
            marked[n++] = obj;
        }
    }
    sub_00048A10_gen();
    for (int i = 0; i < n; i++) MEM32(marked[i] + 0xE8) &= ~FLAG_SEE_THROUGH;
}
