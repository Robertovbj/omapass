#pragma once

class QWindow;

namespace WindowPrivacy {

// Prevents the OS from including `window` in screenshots and screen shares.
// Implemented natively on Windows (WDA_EXCLUDEFROMCAPTURE) and macOS
// (NSWindowSharingNone). No-op on Linux, where the compositor controls this.
void excludeFromCapture(QWindow *window);

}
