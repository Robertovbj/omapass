import QtQuick
import QtQuick.Controls

// A tick box in the Omarchy palette — the Basic style draws its own against
// its palette instead. Used for "show the secret" next to password fields
// and for the generator's options.
//
// It takes part in the sheet's Tab chain the same way a Field does: Tab and
// Shift+Tab are reported for the sheet to move the focus, Space ticks it
// (CheckBox's own handling), Enter submits and Escape cancels. The label
// takes the annotation colour while it has focus, like a Field's label.
CheckBox {
    id: root

    readonly property real s: systemTheme.textScale

    signal submitted()
    signal cancelled()
    signal nextRequested()
    signal previousRequested()

    font.pixelSize: Math.round(12 * s)
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

    indicator: Rectangle {
        implicitWidth: Math.round(16 * root.s)
        implicitHeight: Math.round(16 * root.s)
        y: (root.height - height) / 2
        radius: Math.round(3 * root.s)
        color: "transparent"
        border.width: Math.max(1, Math.round(root.s))
        border.color: root.checked || root.activeFocus ? theme.annotation : theme.guidance

        Rectangle {
            anchors.centerIn: parent
            width: parent.width / 2
            height: parent.height / 2
            radius: Math.round(2 * root.s)
            visible: root.checked
            color: theme.annotation
        }
    }

    contentItem: Text {
        text: root.text
        color: root.activeFocus ? theme.annotation : theme.guidance
        leftPadding: root.indicator.width + Math.round(6 * root.s)
        verticalAlignment: Text.AlignVCenter
        font: root.font
    }
}
