#include "windowprivacy.h"

#include <QWindow>

#import <AppKit/AppKit.h>

namespace WindowPrivacy {

void excludeFromCapture(QWindow *window) {
    if (!window)
        return;
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (!view)
        return;
    NSWindow *nsWindow = [view window];
    if (nsWindow)
        [nsWindow setSharingType:NSWindowSharingNone];
}

}
