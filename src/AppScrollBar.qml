import QtQuick
import QtQuick.Controls

// The scroll bar of a list or a sheet, shown while it scrolls and under the
// pointer, as Omajim's is. SmoothWheel moves the view by setting its position,
// which Flickable does not count as moving, so the bar is kept up for a moment
// after every change of position instead.
ScrollBar {
    id: bar

    required property Flickable view
    readonly property real s: systemTheme.textScale

    active: hovered || pressed || shownLately.running

    contentItem: Rectangle {
        implicitWidth: Math.round(6 * bar.s)
        implicitHeight: Math.round(6 * bar.s)
        radius: width / 2
        color: bar.pressed ? theme.foreground : theme.muted
        opacity: bar.active && bar.size < 1 ? 0.75 : 0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
    }

    Timer {
        id: shownLately
        interval: 900
    }

    Connections {
        target: bar.view
        function onContentYChanged() { shownLately.restart(); }
    }
}
