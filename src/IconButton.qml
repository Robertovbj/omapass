import QtQuick
import QtQuick.Controls

// A square toolbar button with a line icon, lit on hover, as in Hype's
// toolbar. The description is its tooltip and accessible name. Mouse only,
// like AppButton: every action it offers has its key.
ToolButton {
    id: button

    required property string iconName
    required property string description
    readonly property real s: systemTheme.textScale
    readonly property bool lit: hovered || down

    implicitWidth: Math.round(34 * s)
    implicitHeight: Math.round(34 * s)
    padding: 0
    // Hover is asked for, not left to the platform: Qt turns it off where the
    // platform theme says hover effects are not used (qtengine on Omarchy,
    // for one), and then `hovered` never changes.
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: description
    ToolTip.visible: hovered && description !== ""
    ToolTip.delay: 400
    ToolTip.text: description

    contentItem: Item {
        AppIcon {
            anchors.centerIn: parent
            width: Math.round(18 * button.s)
            height: width
            name: button.iconName
            opacity: button.enabled ? 1 : 0.4
            color: button.lit ? theme.foreground : theme.muted
        }
    }

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: button.lit ? theme.hover : "transparent"
    }
}
