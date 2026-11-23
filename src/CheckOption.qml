import QtQuick
import QtQuick.Controls

// A tick box in Hype's style: an accent square with a check, the label in the
// foreground colour. The Basic style would draw its own against its palette. Used for "show the secret" next to password fields
// and for the generator's options.
//
// It takes part in the sheet's Tab chain the same way a Field does: Tab and
// Shift+Tab are reported for the sheet to move the focus, Space ticks it
// (CheckBox's own handling), Enter submits and Escape cancels. Focus shows
// as an accent ring round the box and a tint behind the row.
CheckBox {
    id: root

    readonly property real s: systemTheme.textScale

    signal submitted()
    signal cancelled()
    signal nextRequested()
    signal previousRequested()

    font.pixelSize: Math.round(14 * s)
    spacing: Math.round(10 * s)
    // The whole row, so a long label wraps rather than running off the sheet.
    width: parent ? parent.width : implicitWidth
    topPadding: Math.round(4 * s)
    bottomPadding: Math.round(4 * s)
    leftPadding: Math.round(4 * s)
    rightPadding: Math.round(6 * s)
    // Reached by the keyboard only: a click ticks the box but leaves the
    // focus where it was, so "show password" does not pull the cursor out
    // of the field being typed in.
    focusPolicy: Qt.TabFocus

    Keys.onPressed: function(event) {
        switch (event.key) {
        case Qt.Key_Escape:
            root.cancelled();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.submitted();
            break;
        case Qt.Key_Tab:
            root.nextRequested();
            break;
        case Qt.Key_Backtab:
            root.previousRequested();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: root.activeFocus ? theme.hover : "transparent"
        opacity: 0.6
    }

    indicator: Rectangle {
        implicitWidth: Math.round(18 * root.s)
        implicitHeight: Math.round(18 * root.s)
        x: root.leftPadding
        y: (root.height - height) / 2
        radius: Math.min(3, theme.radius)
        color: root.checked ? theme.accent : theme.background
        border.width: root.activeFocus && !root.checked ? 2 : 1
        border.color: root.checked || root.activeFocus ? theme.accent : theme.border
        opacity: root.enabled ? 1 : 0.5

        AppIcon {
            anchors.centerIn: parent
            width: Math.round(14 * root.s)
            height: width
            visible: root.checked
            name: "check"
            color: theme.accentText
        }
    }

    contentItem: Text {
        text: root.text
        color: root.enabled ? theme.foreground : theme.muted
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        font: root.font
    }
}
