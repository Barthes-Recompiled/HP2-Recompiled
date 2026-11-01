/* xboxrecomp (from the hpps port): one copy of the game at a time. hpcos.exe holds a named mutex for its whole life;
 * a second hpcos.exe while one runs says so, brings the running game's window forward and starts nothing. A second
 * copy would share saves\, shadercache\ and hpcos_run.log with the first. */
#ifndef HPCOS_SINGLE_INSTANCE_H
#define HPCOS_SINGLE_INSTANCE_H

#include <wchar.h>
#include <windows.h>

#define HPCOS_RUNNING_MUTEX L"Local\\hpcos_game_running"
#define HPCOS_GAME_WINDOW_CLASS "XboxRecompFramebuffer" /* fb_present.c */
#define HPCOS_TITLE L"Harry Potter and the Chamber of Secrets"

/* the message, then the running game's window to the front (after the box: a fullscreen game would cover it) */
static inline void hpcos_ReportAlreadyRunning(void)
{
    MessageBoxW(NULL, HPCOS_TITLE L" is already running.", HPCOS_TITLE,
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
    HWND w = FindWindowA(HPCOS_GAME_WINDOW_CLASS, NULL);
    if (w) {
        if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
        SetForegroundWindow(w);
    }
}

/* the game: takes the mutex for the rest of the process; 0 when another copy already has it */
static inline int hpcos_ClaimSingleInstance(void)
{
    HANDLE m = CreateMutexW(NULL, FALSE, HPCOS_RUNNING_MUTEX);
    return !(m && GetLastError() == ERROR_ALREADY_EXISTS);
}

/* whether a game holds the mutex now */
static inline int hpcos_GameRunning(void)
{
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, HPCOS_RUNNING_MUTEX);
    if (!m) return 0;
    CloseHandle(m);
    return 1;
}

#endif
