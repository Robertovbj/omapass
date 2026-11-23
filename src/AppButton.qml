import QtQuick
import QtQuick.Controls

// A text button in Hype's style: the accent for the one main action, the
// theme's button color for the rest, red text for one that destroys.
//
// Mouse only: the sheets are driven from the keyboard by their own keys
// (Enter, Esc, y/n), and a button taking a place in their Tab chains would
// stand between the fields.
Button {
    id: control

    property bool primary: false
    property bool destructive: false
    readonly property real s: systemTheme.textScale
    readonly property bool lit: hovered || down

    focusPolicy: Qt.NoFocus
    implicitHeight: Math.round(36 * s)
    leftPadding: Math.round(16 * s)
    rightPadding: Math.round(16 * s)
    font.pixelSize: Math.round(14 * s)
    font.weight: primary ? Font.DemiBold : Font.Normal

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? theme.muted
             : control.primary && control.destructive ? theme.accentText
             : control.primary ? theme.accentText
             : control.destructive ? theme.error
             : theme.foreground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: Math.round(84 * control.s)
        radius: Math.min(4, theme.radius)
        color: control.primary && control.destructive
                   ? (!control.enabled ? theme.border
                      : control.lit ? Qt.darker(theme.error, 1.15) : theme.error)
             : control.primary ? (!control.enabled ? theme.border
                                  : control.lit ? theme.accentHover : theme.accent)
             : control.lit ? theme.hover
             : control.flat ? "transparent" : theme.button
    }
}
