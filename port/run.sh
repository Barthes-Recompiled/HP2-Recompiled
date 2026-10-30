#!/usr/bin/env bash
# Run hpcos from the xboxrecomp checkout (where game/ lives).
#   RECOMP_TITLE_KEVENTS  events the title builds itself (D3D, worker threads)
#   RECOMP_VBLANK         60 Hz vblank into the title's NV2A ISR
#   RECOMP_AC97_READY     codec ready + emulated APU behind trapped MMIO
#   RECOMP_DSP_ACK        DirectSound's DSP command word (ebx at the GP wait,
#                         scratch + 0x810); the DSP itself is a stub
#   RECOMP_PB_EXEC / RECOMP_FB_WINDOW  execute the pushbuffer, show the framebuffer
cd "$(dirname "$0")/../xboxrecomp" || exit 1
export RECOMP_TITLE_KEVENTS=1 RECOMP_VBLANK=1 RECOMP_AC97_READY=1
export RECOMP_DSP_ACK=0x834D0810
export RECOMP_PB_EXEC=1 RECOMP_FB_WINDOW=1
exec ./hpcos "$@"
