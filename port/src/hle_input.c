/**
 * Trace wrappers around XAPI's controller entry points (RECOMP_PAD_TRACE=1).
 *
 * The emulated USB pad enumerates and is polled, yet the intro does not skip
 * on a press. These show what the title asks XAPI for and what it gets back:
 * which ports it opens, what XInputGetState returns, and when it sees a pad
 * arrive or leave. Nothing here changes behaviour; each wrapper runs the
 * generated XAPI body (sub_X_gen) and only logs.
 *
 * Built into the recompile with a third --exclude-manual naming this file.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* the register names (eax, esp, ...) the runtime macros expand to */
#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"

#define ARG(i)  MEM32(g_esp + 4 + 4 * (i))

static int trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *t = getenv("RECOMP_PAD_TRACE");
        on = t && *t && *t != '0' ? atoi(t) : 0;  /* 2: also every left stick reading */
    }
    return on;
}

/* ── Direct controller access (RECOMP_HLE_XINPUT, default on) ────────────
 *
 * Ported from the original renderer's src/xdk/xapi.cpp: XInitDevices, XInputOpen,
 * XInputClose, XInputGetCapabilities and XInputGetState answer from the PC's
 * pad directly instead of going through the emulated OHCI controller and
 * XAPI's USB driver. That route delivered the stick late (Harry kept walking
 * the old way for a moment, or forward/back when the stick went sideways).
 * XGetDevices / XGetDeviceChanges stay XAPI's own code: they only read the
 * device type record that XInitDevices and the poller below keep current.
 * RECOMP_HLE_XINPUT=0 runs the XAPI bodies over the emulated USB as before. */
#include "input/xinput_xbox.h"

void hpcos_TrainerInject(XBOX_GAMEPAD *g);  /* hle_trainer.c: input a tool asks for, added to port 0 */

extern int app_KbmOn(void);  /* renderer/app/input.cpp: keyboard and mouse play as controller 1 */
extern int app_InputPad(int port, XBOX_GAMEPAD *g);  /* renderer/app/input.cpp: the controls settings; 1 | 2 the sticks
                                                       * keyboard and mouse drove */
extern void app_InputRumble(uint16_t *left, uint16_t *right);  /* renderer/app/input.cpp: vibration settings */

/* The console's ports from the PC's controllers in the order Windows numbers them, gaps closed up: the first
 * controller plugged in is port 0 whichever XInput slot it has (a pad in slot 1 alone was port 1, the keyboard and
 * mouse on port 0 went unread, and unplugging it asked for its controller back). Keyboard and mouse share port 0
 * with that controller, and stand in for it while none is there, so the two take turns freely. The poller
 * (update_ports) remakes the table; -1 no controller. */
static volatile LONG g_host[4] = {-1, -1, -1, -1};

/* One player: every PC controller drives port 0, together with keyboard and mouse, and ports 1-3 stay empty. Under
 * Wine one pad can show up in two XInput slots (Steam Input and the driver), and the game listens only to the port
 * that pressed START: with the slots on ports 0 and 1, the pad on 1 and keyboard and mouse on 0 never took over from
 * each other. RECOMP_ONE_PLAYER=0 gives each controller its own port again. */
static volatile LONG g_slots;  /* XInput slots with a controller, bit per slot */

static int one_player(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_ONE_PLAYER");
        on = !(e && *e == '0');
    }
    return on;
}

static void remap_ports(void)
{
    LONG host[4] = {-1, -1, -1, -1};
    int n = 0;
    LONG slots = 0;
    for (DWORD h = 0; h < 4; h++) {
        XBOX_INPUT_STATE s;
        if (xbox_InputGetState(h, &s) == 0) {
            host[n++] = (LONG)h;
            slots |= 1L << h;
        }
    }
    InterlockedExchange(&g_slots, slots);
    if (one_player()) host[1] = host[2] = host[3] = -1;
    for (int p = 0; p < 4; p++) {
        if (g_host[p] != host[p])
            fprintf(stderr, "[INPUT] port %d now %s%ld\n", p, host[p] < 0 ? "no controller" : "XInput slot ",
                    host[p] < 0 ? 0L : host[p]);
        InterlockedExchange(&g_host[p], host[p]);
    }
}

/* a pad read, port 0 standing in at rest for keyboard and mouse when no controller is there */
static void merge_stick(int16_t *x, int16_t *y, int16_t ox, int16_t oy)
{
    if ((int)ox * ox + (int)oy * oy > (int)*x * *x + (int)*y * *y) *x = ox, *y = oy;
}

/* every controller's state on port 0 at once: a button held on any, the furthest tilt of each stick */
static DWORD read_all(XBOX_INPUT_STATE *s)
{
    DWORD rc = 1167;
    LONG slots = g_slots;
    for (DWORD h = 0; h < 4; h++) {
        XBOX_INPUT_STATE t;
        if (!(slots & (1L << h)) || xbox_InputGetState(h, &t) != 0) continue;
        if (rc != 0) {
            *s = t;
            rc = 0;
            continue;
        }
        s->dwPacketNumber += t.dwPacketNumber;
        s->Gamepad.wButtons |= t.Gamepad.wButtons;
        for (int i = 0; i < 8; i++)
            if (t.Gamepad.bAnalogButtons[i] > s->Gamepad.bAnalogButtons[i])
                s->Gamepad.bAnalogButtons[i] = t.Gamepad.bAnalogButtons[i];
        merge_stick(&s->Gamepad.sThumbLX, &s->Gamepad.sThumbLY, t.Gamepad.sThumbLX, t.Gamepad.sThumbLY);
        merge_stick(&s->Gamepad.sThumbRX, &s->Gamepad.sThumbRY, t.Gamepad.sThumbRX, t.Gamepad.sThumbRY);
    }
    return rc;
}

static DWORD pad_read(DWORD port, XBOX_INPUT_STATE *s)
{
    LONG h = port < 4 ? g_host[port] : -1;
    DWORD rc = one_player() && port == 0 ? read_all(s) : h >= 0 ? xbox_InputGetState((DWORD)h, s) : 1167;
    if (rc != 0 && port == 0 && app_KbmOn()) {
        memset(s, 0, sizeof(*s));
        rc = 0;
    }
    return rc;
}

#define XPP_GAMEPAD_TYPE 0x00176294u  /* g_DeviceType_Gamepad: Current, Change, Previous connected */
#define XPP_MU_TYPE      0x00176340u  /* g_DeviceType_MU */
#define HANDLE_BASE      0x5ADE0010u  /* opaque handles, never dereferenced by the title */
#define MAX_HANDLES      32

static int hle_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_HLE_XINPUT");
        on = !(e && *e == '0');
        fprintf(stderr, "[INPUT] XInput %s\n", on ? "direct" : "through the emulated USB");
    }
    return on;
}

static CRITICAL_SECTION g_pad_lock;
/* One record per XInputOpen, as the original's g_handles: a title can hold two handles
 * on one port (the menu's and the game's) and close either. A handle per port
 * let the menu's close kill the game's, which then read "not connected". */
static struct { int used; uint32_t port, packet; XBOX_GAMEPAD last; } g_h[MAX_HANDLES];

static void update_ports(void)
{
    uint32_t cur = 0, old;
    remap_ports();
    /* read each pad: on Windows xbox_InputIsConnected is only a cache the reads update */
    for (DWORD p = 0; p < 4; p++) {
        XBOX_INPUT_STATE s;
        if (pad_read(p, &s) == 0) cur |= 1u << p;
    }
    EnterCriticalSection(&g_pad_lock);
    old = MEM32(XPP_GAMEPAD_TYPE);
    if (cur != old) {
        MEM32(XPP_GAMEPAD_TYPE + 4) |= cur ^ old;
        MEM32(XPP_GAMEPAD_TYPE) = cur;
        fprintf(stderr, "[INPUT] controller ports now %X (was %X)\n", cur, old);
    }
    LeaveCriticalSection(&g_pad_lock);
}

static DWORD WINAPI port_poller(LPVOID unused)
{
    (void)unused;
    for (;;) {
        Sleep(250);
        update_ports();
    }
    return 0;
}

static int handle_index(uint32_t h)
{
    uint32_t i = (h - HANDLE_BASE) / 16;
    if (h < HANDLE_BASE || (h - HANDLE_BASE) % 16 || i >= MAX_HANDLES || !g_h[i].used) return -1;
    return (int)i;
}

static int handle_port(uint32_t h)
{
    int i = handle_index(h);
    return i < 0 ? -1 : (int)g_h[i].port;
}

/* XInitDevices(dwPreallocTypeCount, PreallocTypes) */
extern void sub_00176729_gen(void);
void sub_00176729(void)
{
    static int poller;
    if (!hle_on()) {
        sub_00176729_gen();
        return;
    }
    InitializeCriticalSection(&g_pad_lock);
    xbox_InputInit();
    for (int i = 0; i < 3; i++) {
        MEM32(XPP_GAMEPAD_TYPE + 4 * i) = 0;
        MEM32(XPP_MU_TYPE + 4 * i) = 0;
    }
    /* No update_ports() here, unlike the original: on the console the pads enumerate over
     * USB after XInitDevices returns, so a title sees each one arrive as an
     * insertion from XGetDeviceChanges. HP:CoS sets its per-port "connected" flag
     * (0x51E0BC + port * 0x38) only on that insertion; a pad already present at
     * XInitDevices was consumed by its boot-time XGetDevices, the flag stayed 0 and
     * gameplay asked to connect a controller. The poller reports them 250 ms in. */
    if (!poller) {
        poller = 1;
        CloseHandle(CreateThread(NULL, 0, port_poller, NULL, 0, NULL));
    }
    g_esp += 4 + 8;
}

/* XInputOpen(DeviceType, dwPort, dwSlot, pPollingParameters) -> HANDLE */
extern void sub_001771FB_gen(void);
void sub_001771FB(void)
{
    uint32_t type = ARG(0), port = ARG(1), slot = ARG(2);
    if (hle_on()) {
        g_eax = 0;
        if (type == XPP_GAMEPAD_TYPE && port < 4) {
            for (int i = 0; i < MAX_HANDLES; i++) {
                if (g_h[i].used) continue;
                memset(&g_h[i], 0, sizeof(g_h[i]));
                g_h[i].used = 1;
                g_h[i].port = port;
                g_eax = HANDLE_BASE + 16u * (uint32_t)i;
                break;
            }
        }
        g_esp += 4 + 16;
    } else {
        sub_001771FB_gen();
    }
    if (trace_on())
        fprintf(stderr, "[PADTRACE] XInputOpen(type %08X, port %u, slot %u) = %08X\n",
                type, port, slot, g_eax);
}

/* XInputClose(hDevice) */
extern void sub_00177251_gen(void);
void sub_00177251(void)
{
    int p;
    if (!hle_on()) {
        sub_00177251_gen();
        return;
    }
    p = handle_index(ARG(0));
    if (p >= 0) g_h[p].used = 0;
    if (trace_on())
        fprintf(stderr, "[PADTRACE] XInputClose(%08X)\n", ARG(0));
    g_esp += 4 + 4;
}

/* XInputGetCapabilities(hDevice, pCapabilities) -> DWORD. XINPUT_CAPABILITIES:
 * BYTE SubType, WORD Reserved (at 2), XINPUT_GAMEPAD In (at 4), XINPUT_RUMBLE Out (at 22). */
extern void sub_0017725D_gen(void);
void sub_0017725D(void)
{
    uint32_t caps = ARG(1);
    int p;
    if (!hle_on()) {
        sub_0017725D_gen();
        return;
    }
    p = handle_port(ARG(0));
    if (p < 0) {
        g_eax = 6;                          /* ERROR_INVALID_HANDLE */
    } else if (!(p < 4 && g_host[p] >= 0) && !(p == 0 && app_KbmOn())) {
        g_eax = 1167;                       /* ERROR_DEVICE_NOT_CONNECTED */
    } else {
        MEM8(caps) = 1;                     /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
        MEM8(caps + 1) = 0;
        MEM16(caps + 2) = 0;
        MEM16(caps + 4) = 0x00FF;           /* every digital button */
        for (int i = 0; i < 8; i++) MEM8(caps + 6 + i) = 0xFF;
        for (int i = 0; i < 4; i++) MEM16(caps + 14 + 2 * i) = 0xFFFF;
        MEM16(caps + 22) = 0xFFFF;          /* both motors */
        MEM16(caps + 24) = 0xFFFF;
        g_eax = 0;
    }
    g_esp += 4 + 8;
}

extern int hpcos_MenuOpen(void);  /* renderer/app/menu.cpp */

/* Stick range compensation (RECOMP_STICK_RANGE, default 0.65; 1 = off).
 *
 * The game runs only when the stick reads past 0.5 after its own 0.35
 * deadzone (0x19050, test at 0x62885): about 22100 raw. A Controller S gives
 * 32767 at full tilt, but some PC pads report a short range in one direction
 * (one reported -21900 held straight back, 32767 forward), so Harry walked
 * backwards. Deflection at the given fraction of full now counts as full,
 * scaled radially so the direction is kept. */
static float stick_range(void)
{
    static float r = -1.0f;
    if (r < 0.0f) {
        const char *e = getenv("RECOMP_STICK_RANGE");
        r = e && *e ? (float)atof(e) : 0.65f;
        if (r <= 0.05f || r > 1.0f) r = 1.0f;
    }
    return r;
}

static void stick_compensate(SHORT *x, SHORT *y)
{
    float r = stick_range();
    if (r >= 1.0f) return;
    float fx = *x / 32767.0f, fy = *y / 32767.0f;
    float m = sqrtf(fx * fx + fy * fy);
    if (m <= 0.0f) return;
    float s = 1.0f / r;
    if (m * s > 1.0f) s = 1.0f / m;  /* keep the result on the unit circle */
    fx *= s; fy *= s;
    if (fx > 1.0f) fx = 1.0f; else if (fx < -1.0f) fx = -1.0f;
    if (fy > 1.0f) fy = 1.0f; else if (fy < -1.0f) fy = -1.0f;
    *x = (SHORT)lrintf(fx * 32767.0f);
    *y = (SHORT)lrintf(fy * 32767.0f);
}

extern int app_MoveMode(void);  /* renderer/app/input.cpp: the movement keys' pace, 0 slow walk 1 walk 2 run */

/* Slow walk picked on the mouse wheel while Harry walks. His gait follows his speed through the table at 0x1E90C8
 * (record +0x14A: 0 slow walk up to 0.04, 1 walk 0 to 0.079, 2 run from 0.05) and changes only when the speed leaves
 * the current gait's range (0x630D0). Walk's starts at 0, so slowing down never leaves it: on the console a walk
 * stays a walk until Harry stops. Marked running once his speed is a slow walk's, the game's own check finds the
 * speed below running's range and changes his gait down (0x63550, the animation blended as for any change). Called
 * on the game's thread as it reads the pad, in the frame before its movement update. */
static void move_slow_walk(void)
{
    uint32_t ent = MEM32(0x530408), mv = ent ? MEM32(ent + 0xBC) : 0;
    if (!mv || app_MoveMode() != 0 || MEM16(0x530298) != 3 || MEM8(mv + 0x14A) != 1) return;
    float speed = MEMF(mv + 0x10C);
    if (speed > 0.0f && speed < 0.04f) MEM8(mv + 0x14A) = 2;
}

/* RECOMP_MOVE_TRACE=1: Harry's movement as the game keeps it, a line per 33 ms while the left stick or his speed is
 * not 0: the pad's left stick, the game's own (0x51E094/98, past its deadzones), the movement state (0x530298) and
 * his movement record ([[0x530408] + 0xBC]): +0xE0 speed, +0x10C speed, +0xE8 / +0xF4 the ramped value and its
 * target, +0xEC / +0xF0 their steps, +0x14A, and how far he moved since the last line (object +0x14/1C) */
static void move_trace(const XBOX_GAMEPAD *g)
{
    static int on = -1;
    static DWORD last;
    static float px, pz;
    if (on < 0) {
        const char *e = getenv("RECOMP_MOVE_TRACE");
        on = e && *e && *e != '0';
    }
    if (!on || GetTickCount() - last < 33) return;
    uint32_t ent = MEM32(0x530408), mv = ent ? MEM32(ent + 0xBC) : 0, obj = ent ? MEM32(ent + 0xAC) : 0;
    if (!mv || !obj) return;
    float x = MEMF(obj + 0x14), z = MEMF(obj + 0x1C), d = sqrtf((x - px) * (x - px) + (z - pz) * (z - pz));
    float sp = MEMF(mv + 0x10C);
    px = x, pz = z;
    if (!g->sThumbLX && !g->sThumbLY && sp == 0.0f && d < 1e-4f) return;
    last = GetTickCount();
    fprintf(stderr, "[MOVE] %lu stick %6d %6d game %.3f %.3f state %u e0 %.4f 10c %.4f e8 %.3f f4 %.3f ec %.3f f0 %.3f 14a %u moved %.4f\n",
            (unsigned long)last, g->sThumbLX, g->sThumbLY, MEMF(0x51E094), MEMF(0x51E098), MEM16(0x530298),
            MEMF(mv + 0xE0), sp, MEMF(mv + 0xE8), MEMF(mv + 0xF4), MEMF(mv + 0xEC), MEMF(mv + 0xF0),
            MEM8(mv + 0x14A), d);
}

/* the pad as the game last read it: the debug trainer's noclip flies Harry with it (hle_trainer.c) */
XBOX_GAMEPAD hpcos_last_pad;

static void get_state_direct(void)
{
    uint32_t st = ARG(1);
    int h = handle_index(ARG(0));
    XBOX_INPUT_STATE s;
    if (h < 0) {
        g_eax = 6;
    } else if (pad_read((DWORD)g_h[h].port, &s) != 0) {
        g_eax = 1167;
    } else {
        /* the F10 options panel has the pad while it is open: the game sees it at rest */
        if (hpcos_MenuOpen()) memset(&s.Gamepad, 0, sizeof(s.Gamepad));
        /* the controls settings: remapping, deadzones, camera inversion, keyboard and mouse on port 0 */
        int kbm = app_InputPad((int)g_h[h].port, &s.Gamepad);
        /* a PC pad's short range; the movement keys give the tilt they mean (the mouse camera keeps it: its
         * sensitivity was tuned with it) */
        if (!(kbm & 1)) stick_compensate(&s.Gamepad.sThumbLX, &s.Gamepad.sThumbLY);
        else if (g_h[h].port == 0) move_slow_walk();
        stick_compensate(&s.Gamepad.sThumbRX, &s.Gamepad.sThumbRY);
        if (g_h[h].port == 0) hpcos_TrainerInject(&s.Gamepad);
        /* Alohomora: the L trigger (aim) pressed during its cast moved the spell's light up and to the side
         * (2026-10-25). While Harry's animation record ([0x530354]) is Alohomora's cast (0x1ECEBC), the game sees the
         * trigger released, unless it was already held when the cast began (aimed first, then cast).
         * RECOMP_ALOHOMORA_AIM=1: as the Xbox */
        if (g_h[h].port == 0) {
            static int fix = -1, casting, held_at_start;
            if (fix < 0) {
                const char *e = getenv("RECOMP_ALOHOMORA_AIM");
                fix = !(e && e[0] == '1');
            }
            int now = MEM32(0x530354) == 0x001ECEBCu;
            if (now && !casting) held_at_start = s.Gamepad.bAnalogButtons[6] > 30;
            casting = now;
            if (fix && casting && !held_at_start) s.Gamepad.bAnalogButtons[6] = 0;
        }
        /* the packet number moves only when something changed, as XAPI's does */
        if (memcmp(&s.Gamepad, &g_h[h].last, sizeof(s.Gamepad))) g_h[h].packet++;
        g_h[h].last = s.Gamepad;
        hpcos_last_pad = s.Gamepad;
        if (g_h[h].port == 0) move_trace(&s.Gamepad);
        MEM32(st) = g_h[h].packet;
        MEM16(st + 4) = s.Gamepad.wButtons;
        for (int i = 0; i < 8; i++) MEM8(st + 6 + i) = s.Gamepad.bAnalogButtons[i];
        MEM16(st + 14) = (uint16_t)s.Gamepad.sThumbLX;
        MEM16(st + 16) = (uint16_t)s.Gamepad.sThumbLY;
        MEM16(st + 18) = (uint16_t)s.Gamepad.sThumbRX;
        MEM16(st + 20) = (uint16_t)s.Gamepad.sThumbRY;
        g_eax = 0;
    }
    g_esp += 4 + 8;
}

/* XInputGetState(hDevice, pState) -> DWORD. XINPUT_STATE: dwPacketNumber,
 * then XINPUT_GAMEPAD { wButtons, bAnalogButtons[8], sThumb* }. Logs every
 * change of result, buttons or the A/B/X/Y pressure, and a heartbeat. */
extern void sub_0017743B_gen(void);
void sub_0017743B(void)
{
    static uint32_t last_rc = 0xFFFFFFFF, last_btn = 0xFFFFFFFF, last_abxy = 0xFFFFFFFF, calls;
    static DWORD t0;
    uint32_t dev = ARG(0), st = ARG(1);
    if (hle_on())
        get_state_direct();
    else
        sub_0017743B_gen();
    if (!trace_on())
        return;
    calls++;
    {
        uint32_t rc = g_eax, btn = st ? MEM16(st + 4) : 0, abxy = st ? MEM32(st + 6) : 0;
        DWORD now = GetTickCount();
        if (rc != last_rc || btn != last_btn || abxy != last_abxy)
            fprintf(stderr, "[PADTRACE] XInputGetState(%08X port %d) = %u pkt=%u buttons=%04X ABXY=%08X\n",
                    dev, handle_port(dev), rc, st ? MEM32(st) : 0, btn, abxy);
        last_rc = rc; last_btn = btn; last_abxy = abxy;
        /* the left stick as the game gets it, every call, with the time: a flick that reads centred for several
         * frames or repeats a stale value shows input arriving late */
        if (st && trace_on() > 1) {
            LARGE_INTEGER q, f;
            QueryPerformanceCounter(&q);
            QueryPerformanceFrequency(&f);
            fprintf(stderr, "[STICK] %.1f %d %d pkt=%u\n", q.QuadPart * 1000.0 / f.QuadPart,
                    (int16_t)MEM16(st + 14), (int16_t)MEM16(st + 16), MEM32(st));
        }
        if (now - t0 >= 5000) {
            if (hle_on())   /* our handles are not XAPI device records */
                fprintf(stderr, "[PADTRACE] %u XInputGetState calls in %lu ms; handle %08X\n",
                        calls, (unsigned long)(now - t0), dev);
            else
                fprintf(stderr, "[PADTRACE] %u XInputGetState calls in %lu ms; handle %08X pkt=%u "
                        "data %08X %08X %08X\n", calls, (unsigned long)(now - t0), dev,
                        MEM32(dev + 8), MEM32(dev + 0x14), MEM32(dev + 0x18), MEM32(dev + 0x1C));
            calls = 0;
            t0 = now;
        }
    }
}

/* XGetDeviceChanges(DeviceType, pdwInsertions, pdwRemovals) -> BOOL */
extern void sub_00177523_gen(void);
void sub_00177523(void)
{
    uint32_t type = ARG(0), ins = ARG(1), rem = ARG(2);
    sub_00177523_gen();
    if (trace_on() && g_eax)
        fprintf(stderr, "[PADTRACE] XGetDeviceChanges(type %08X): insertions %08X removals %08X\n",
                type, ins ? MEM32(ins) : 0, rem ? MEM32(rem) : 0);
}

/* XGetDevices(DeviceType) -> bitmask of connected ports */
extern void sub_00177501_gen(void);
void sub_00177501(void)
{
    uint32_t type = ARG(0);
    sub_00177501_gen();
    if (trace_on())
        fprintf(stderr, "[PADTRACE] XGetDevices(type %08X) = %08X\n", type, g_eax);
}

/* XInputSetState(hDevice, pFeedback) -> DWORD.
 *
 * Replaced, not traced: XAPI sends the rumble report down the pad's interrupt
 * OUT pipe and completes it later, setting pFeedback->Header.dwStatus and the
 * header's event. That completion never arrives from the emulated OHCI, and
 * the title waits on the event with INFINITE (sub_00018F20, its rumble helper):
 * the game froze the first time Harry was hit (washing-machine duel). Here the
 * rumble goes to the PC's pad and the request completes at once.
 *
 * XINPUT_FEEDBACK: header { dwStatus, hEvent, Reserved[58] } at 0, then
 * XINPUT_RUMBLE { wLeftMotorSpeed, wRightMotorSpeed } at 0x42. */
extern void xbox_GuestSetEvent(uint32_t handle);

void sub_001774A7(void)
{
    uint32_t fb = ARG(1);
    XBOX_VIBRATION v = {0, 0};
    int port = hle_on() ? handle_port(ARG(0)) : 0;
    uint32_t event;

    if (fb) {
        v.wLeftMotorSpeed = MEM16(fb + 0x42);
        v.wRightMotorSpeed = MEM16(fb + 0x44);
        app_InputRumble(&v.wLeftMotorSpeed, &v.wRightMotorSpeed);
        LONG h = g_host[port < 0 || port > 3 ? 0 : port];  /* the PC controller on that port (remap_ports) */
        if (one_player() && port == 0) {
            LONG slots = g_slots;  /* every pad that drives port 0 */
            for (DWORD i = 0; i < 4; i++)
                if (slots & (1L << i)) xbox_InputSetState(i, &v);
        } else if (h >= 0) {
            xbox_InputSetState((DWORD)h, &v);
        }
        event = MEM32(fb + 4);
        MEM32(fb) = 0;              /* ERROR_SUCCESS */
        if (event)
            xbox_GuestSetEvent(event);
    }
    if (trace_on())
        fprintf(stderr, "[PADTRACE] XInputSetState(%08X) rumble %04X %04X\n", ARG(0), v.wLeftMotorSpeed, v.wRightMotorSpeed);
    g_eax = 997;                    /* ERROR_IO_PENDING, as XAPI returns */
    g_esp += 4 + 8;
}
