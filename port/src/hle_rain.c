/**
 * Rain as one batched draw: sub_000387F0, the title's rain, recompiled with its per-drop DrawVerticesUP replaced
 * (below). Hand-written over the generated body: regen.sh passes --exclude-manual port/src/hle_rain.c, so the
 * generator's own copy is sub_000387F0_gen (unused). Moved here from gen/recomp_extra.c (2026-10-25), where a
 * regeneration would have lost it.
 */

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_funcs.h"
#include <math.h>
#include <string.h>

/* xboxrecomp: sub_000387F0 draws rain one DrawVerticesUP line per drop. The console takes thousands of those a frame
 * in stride; each one is a whole draw on D3D12 (30 fps in rain). The drops go into one guest buffer instead and are
 * drawn by a single DrawVerticesUP of the same primitive and stride once the loop ends: the same lines, same state. */
uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);

static uint32_t g_rain_buf, g_rain_cap, g_rain_used, g_rain_prim, g_rain_stride;

static void rain_flush(void)
{
    if (g_rain_used && g_rain_stride) {
        PUSH32(esp, g_rain_stride);
        PUSH32(esp, g_rain_buf);
        PUSH32(esp, g_rain_used / g_rain_stride);
        PUSH32(esp, g_rain_prim);
        PUSH32(esp, 0x00038920u); RECOMP_ABI_CALL(0x00135E60u, sub_00135E60);
    }
    g_rain_used = 0;
}

/* at the call: [esp] primitive, [esp + 4] vertex count, [esp + 8] vertices, [esp + 12] stride (stdcall, 16 bytes) */
static void rain_add(void)
{
    uint32_t prim = MEM32(esp), count = MEM32(esp + 4), src = MEM32(esp + 8), stride = MEM32(esp + 12);
    uint32_t n = count * stride;
    esp += 16;
    if (!n) return;
    if (g_rain_used && (prim != g_rain_prim || stride != g_rain_stride)) rain_flush();
    if (g_rain_used + n > g_rain_cap) {
        if (g_rain_used) rain_flush();
        if (n > g_rain_cap) {
            uint32_t cap = g_rain_cap ? g_rain_cap : 0x10000;
            while (cap < n) cap *= 2;
            uint32_t buf = xbox_HeapAlloc(cap, 16);
            if (!buf) {  /* no room: draw this one as the title did */
                PUSH32(esp, stride); PUSH32(esp, src); PUSH32(esp, count); PUSH32(esp, prim);
                PUSH32(esp, 0x00038920u); RECOMP_ABI_CALL(0x00135E60u, sub_00135E60);
                return;
            }
            g_rain_buf = buf, g_rain_cap = cap;
        }
    }
    memcpy((void *)XBOX_PTR(g_rain_buf + g_rain_used), (const void *)XBOX_PTR(src), n);
    g_rain_used += n, g_rain_prim = prim, g_rain_stride = stride;
}

/**
 * sub_000387F0
 * Original: 0x000387F0 - 0x00038931 (321 bytes, 92 insns)
 * CC: cdecl, 0 params, returns int_or_void
 * Frame: fpo_leaf
 */
void sub_000387F0(void)
{
    int _flags = 0; /* fallback flag var */
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_fa; (void)_fb; (void)_fas; (void)_fbs;
    #define fp_push(v) do { double _fp_value = (v); \
        g_fp_top = (g_fp_top + 7u) & 7u; \
        g_fp_stack[g_fp_top] = _fp_value; } while (0)
    #define fp_pop() (g_fp_top = (g_fp_top + 1u) & 7u)
    #define fp_top() g_fp_stack[g_fp_top]
    #define fp_st(i) g_fp_stack[(g_fp_top + (i)) & 7u]
    #define fp_st1() fp_st(1)

loc_000387F0: ;
    _fb = (uint32_t)(0x54) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* sub source, before the write */
    esp = esp - 0x54;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* sub result */
    PUSH32(esp, esi);
    esi = MEM32(ecx + 0x230);
    _fa = (uint32_t)(esi) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp esi, 0 (32-bit) */
    if (CMP_EQ(_fa, _fb)) goto loc_0003892C; /* je: equal / zero */

loc_00038802: ;
    ecx = esp + 0x28;
    PUSH32(esp, 0x0003880Bu); RECOMP_ABI_CALL(0x00036CA0u, sub_00036CA0); /* call 0x00036CA0 */

loc_0003880B: ;
    eax = esp + 0x28;
    PUSH32(esp, eax);
    MEM32(esp + 0x50) = 0;
    MEM32(esp + 0x54) = 0;
    MEM32(esp + 0x58) = 0;
    PUSH32(esp, 0x0003882Du); RECOMP_ABI_CALL(0x000153B0u, sub_000153B0); /* call 0x000153B0 */

loc_0003882D: ;
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esp = esp + 4;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    PUSH32(esp, 0);
    PUSH32(esp, 0);
    PUSH32(esp, 0x00038839u); RECOMP_ABI_CALL(0x00133520u, sub_00133520); /* call 0x00133520 */

loc_00038839: ;
    ecx = MEM32(0x214474);
    PUSH32(esp, ecx);
    PUSH32(esp, 0x00038845u); RECOMP_ABI_CALL(0x00132CA0u, sub_00132CA0); /* call 0x00132CA0 */

loc_00038845: ;
    PUSH32(esp, edi);
    goto loc_00038850;

    /* nop */
    /* nop */

loc_00038850: ;
    edx = MEM32(esi);
    fp_push(MEMF(0x51E024)); /* fld float */
    g_fp_cmp = RECOMP_FCMP(fp_top(), MEMF(0x17DF18)); g_fp_cc = RECOMP_FCMP_CC(g_fp_cmp); fp_pop(); /* fcomp dword ptr [0x17df18] */
    MEM32(esp + 0xC) = edx;
    eax = MEM32(esi + 4);
    MEM32(esp + 0x10) = eax;
    ecx = MEM32(esi + 8);
    eax = (eax & 0xFFFF0000u) | (uint32_t)(uint16_t)(((g_fp_top & 7u) << 11) | g_fp_cc); /* fnstsw ax <- fpu status */
    MEM32(esp + 0x14) = ecx;
    _fa = (uint32_t)(HI8(eax)) & 0xFFu; _fb = (uint32_t)(0x44) & 0xFFu;
    _fas = (int32_t)(int8_t)(_fa); _fbs = (int32_t)(int8_t)(_fb); /* test HI8(eax), 0x44 (8-bit) */
    if ((!RECOMP_PARITY8((_fa) & (_fb)))) goto loc_000388ED; /* jnp: not parity */

loc_00038877: ;
    edi = MEM32(esi + 0x18);
    fp_push(MEMF(0x17DEC8)); /* fld float */
    fp_top() = RECOMP_FP_PC(fp_top() - MEMF(0x51E024)); /* fsub dword ptr [0x51e024] */
    edx = edi;
    edx = edx >> 0x18;
    _fa = (uint32_t)(edx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    _fa = (uint32_t)(edx) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp edx, 0 (32-bit) */
    MEM32(esp + 8) = edx;
    fp_push((double)SMEM32(esp + 8)); /* fild */
    if (CMP_GE(_fas, _fbs)) goto loc_0003889D; /* jge: greater or equal (signed >=) */

loc_00038897: ;
    fp_top() = RECOMP_FP_PC(fp_top() + MEMF(0x17DECC)); /* fadd dword ptr [0x17decc] */

loc_0003889D: ;
    fp_top() = RECOMP_FP_PC(fp_top() * fp_st1()); /* fmul st(1) */
    PUSH32(esp, 0x000388A4u); RECOMP_ABI_CALL(0x001273F4u, sub_001273F4); /* call 0x001273F4 */

loc_000388A4: ;
    eax = ZX8(LO8(eax));
    edi = edi & 0xFFFFFF;
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */
    eax = eax << 0x18;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    eax = eax | edi;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* or result */
    MEM32(esp + 0x18) = eax;
    edi = MEM32(esi + 0x1C);
    ecx = edi;
    ecx = ecx >> 0x18;
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ecx, 0 (32-bit) */
    MEM32(esp + 8) = ecx;
    fp_push((double)SMEM32(esp + 8)); /* fild */
    if (CMP_GE(_fas, _fbs)) goto loc_000388D0; /* jge: greater or equal (signed >=) */

loc_000388CA: ;
    fp_top() = RECOMP_FP_PC(fp_top() + MEMF(0x17DECC)); /* fadd dword ptr [0x17decc] */

loc_000388D0: ;
    fp_top() = RECOMP_FP_PC(fp_top() * fp_st1()); /* fmul st(1) */
    PUSH32(esp, 0x000388D7u); RECOMP_ABI_CALL(0x001273F4u, sub_001273F4); /* call 0x001273F4 */

loc_000388D7: ;
    fp_pop(); /* fstp st(0) */
    edx = ZX8(LO8(eax));
    edx = edx << 0x18;
    _fa = (uint32_t)(edx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* shift result */
    edi = edi & 0xFFFFFF;
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */
    edx = edx | edi;
    _fa = (uint32_t)(edx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* or result */
    MEM32(esp + 0x28) = edx;
    goto loc_000388FB;

loc_000388ED: ;
    eax = MEM32(esi + 0x18);
    MEM32(esp + 0x18) = eax;
    ecx = MEM32(esi + 0x1C);
    MEM32(esp + 0x28) = ecx;

loc_000388FB: ;
    edx = MEM32(esi + 0xC);
    MEM32(esp + 0x1C) = edx;
    eax = MEM32(esi + 0x10);
    PUSH32(esp, 0x10);
    edx = esp + 0x10;
    PUSH32(esp, edx);
    MEM32(esp + 0x28) = eax;
    ecx = MEM32(esi + 0x14);
    PUSH32(esp, 2);
    PUSH32(esp, 2);
    MEM32(esp + 0x34) = ecx;
    rain_add(); /* call 0x00135E60, batched */

loc_00038920: ;
    esi = MEM32(esi + 0x20);
    _fa = (uint32_t)(esi) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp esi, 0 (32-bit) */
    if (CMP_NE(_fa, _fb)) goto loc_00038850; /* jne: not equal / not zero */

loc_0003892B: ;
    rain_flush();
    POP32(esp, edi);

loc_0003892C: ;
    POP32(esp, esi);
    _fb = (uint32_t)(0x54) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esp = esp + 0x54;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */
    esp += 4; return; /* ret */

    #undef fp_push
    #undef fp_pop
    #undef fp_top
    #undef fp_st
    #undef fp_st1
}
