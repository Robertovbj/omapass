import QtQuick

// A choice among a few, side by side, in the look of Omajim's AppSegmented:
// tabs over what they switch. From the keyboard it takes part in a sheet's
// Tab chain like a Field: ←/→ (or h/l, Space) move between the options, Tab
// and Shift+Tab are reported for the sheet to move the focus, Enter submits
// and Escape cancels.
FocusScope {
    id: root

    property var options: []
    property int currentIndex: 0
    readonly property real s: systemTheme.textScale

    signal submitted()
    signal cancelled()
    signal nextRequested()
    signal previousRequested()

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.round(34 * s)
    activeFocusOnTab: false

    function step(by) {
        if (options.length > 0)
            currentIndex = (currentIndex + by + options.length) % options.length;
    }

    Keys.onPressed: function(event) {
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_H:
            root.step(-1);
            break;
        case Qt.Key_Right:
        case Qt.Key_L:
        case Qt.Key_Space:
            root.step(1);
            break;
        case Qt.Key_Tab:
            root.nextRequested();
            break;
        case Qt.Key_Backtab:
            root.previousRequested();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.submitted();
            break;
        case Qt.Key_Escape:
            root.cancelled();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(4, theme.radius)
        color: theme.background
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? theme.accent : theme.border

        Row {
            anchors.fill: parent
            anchors.margins: 3

            Repeater {
                model: root.options

                Rectangle {
                    id: segment
                    required property var modelData
                    required property int index
                    readonly property bool active: root.currentIndex === index

                    objectName: "segment" + index
                    width: (root.width - 6) / root.options.length
                    height: parent.height
                    radius: Math.min(3, theme.radius)
                    color: active ? theme.selection
                         : segmentHover.hovered ? theme.hover : "transparent"

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 8
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: segment.modelData
                        font.pixelSize: Math.round(13 * root.s)
                        font.weight: segment.active ? Font.DemiBold : Font.Normal
                        color: segment.active ? theme.selectionText : theme.muted
                    }

                    HoverHandler { id: segmentHover }
                    TapHandler { onTapped: root.currentIndex = segment.index }
                }
            }
        }
    }
}
