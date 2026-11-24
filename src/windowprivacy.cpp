#include "windowprivacy.h"

#include <QWindow>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace WindowPrivacy {

void excludeFromCapture(QWindow *window) {
    if (!window)
        return;
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;
    // WDA_EXCLUDEFROMCAPTURE hides the window completely on Windows 10 2004+;
    // fall back to WDA_MONITOR (black box) on older Windows 10 builds.
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE))
        SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
#else
    Q_UNUSED(window)
#endif
}

}
