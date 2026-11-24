import QtQuick
import QtQuick.Controls

Item {
    id: layer
    anchors.fill: parent
    z: 10000
    visible: linkHints.badges.length > 0
    readonly property var hostWindow: Window.window
    onHostWindowChanged: if (hostWindow) linkHints.watch(parent)
    Component.onCompleted: if (hostWindow) linkHints.watch(parent)
    Repeater {
        model: linkHints.badges
        Rectangle {
            required property var modelData
            x: Math.max(0, Math.min(modelData.x, layer.width - width))
            y: Math.max(0, Math.min(modelData.y, layer.height - height))
            width: label.implicitWidth + 8
            height: label.implicitHeight + 2
            radius: Math.min(theme.radius, 3)
            color: theme.accent
            visible: modelData.label.startsWith(linkHints.prefix)
            Text {
                id: label
                anchors.centerIn: parent
                text: parent.modelData.label.slice(linkHints.prefix.length).toUpperCase()
                font.family: systemTheme.monoFamily
                font.pixelSize: Math.round(12 * systemTheme.textScale)
                font.bold: true
                color: theme.accentText
            }
        }
    }
}
