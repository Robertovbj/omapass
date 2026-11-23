import QtQuick
import QtQuick.Controls

// The dropdown that hangs under a text field: existing groups, directory
// completions, GPG keys. Navigated from the field above it with Ctrl+N/P or
// the arrows, so the field keeps the typing focus throughout.
Item {
    id: root

    property var model: []
    property int currentIndex: 0
    property int visibleRows: 4
    // Row label; defaults to the item itself.
    property var labelFor: function(item) { return item; }

    signal picked(int index)

    readonly property real s: systemTheme.textScale
    readonly property int rowHeight: Math.round(26 * s)

    visible: list.count > 0
    height: visible ? Math.min(list.count, visibleRows) * rowHeight + frame.border.width * 2 + 8 : 0

    Rectangle {
        id: frame
        anchors.fill: parent
        color: theme.background
        radius: Math.min(4, theme.radius)
        border.width: 1
        border.color: theme.border
    }

    function next() {
        if (list.count === 0)
            return;
        root.currentIndex = root.currentIndex >= list.count - 1 ? 0 : root.currentIndex + 1;
    }

    function previous() {
        if (list.count === 0)
            return;
        root.currentIndex = root.currentIndex <= 0 ? list.count - 1 : root.currentIndex - 1;
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 4 + frame.border.width
        model: root.model
        clip: true
        currentIndex: root.currentIndex
        highlightMoveDuration: 0
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: AppScrollBar { view: list }

        delegate: Rectangle {
            required property int index
            required property var modelData

            width: list.width
            height: root.rowHeight
            radius: Math.min(3, theme.radius)
            color: root.currentIndex === index ? theme.selection
                 : rowHover.hovered ? theme.hover : "transparent"

            HoverHandler { id: rowHover }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Math.round(8 * root.s)
                text: root.labelFor(modelData)
                color: root.currentIndex === index ? theme.selectionText : theme.foreground
                elide: Text.ElideMiddle
                font.pixelSize: Math.round(13 * root.s)
            }

            TapHandler {
                onTapped: {
                    root.currentIndex = index;
                    root.picked(index);
                }
            }
        }
    }
}
