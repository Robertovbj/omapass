import QtQuick

// A short list of mutually exclusive options, navigated with j/k or the
// arrows and confirmed with Enter. Used for "which kind of database?" and
// the right-click actions menu.
Modal {
    id: root

    property var options: []
    property int selected: 0

    signal chosen(int index)

    onKeyPressed: function(event) {
        switch (event.key) {
        case Qt.Key_Escape:
            root.dismissed();
            break;
        case Qt.Key_Down:
        case Qt.Key_J:
            root.selected = (root.selected + 1) % root.options.length;
            break;
        case Qt.Key_Up:
        case Qt.Key_K:
            root.selected = (root.selected + root.options.length - 1) % root.options.length;
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.chosen(root.selected);
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    // Picking a row is the answer; the button and Esc give up.
    cancelOnly: true

    Repeater {
        model: root.options

        Rectangle {
            required property int index
            required property var modelData

            objectName: "choice" + index

            width: parent.width
            height: Math.round(38 * root.s)
            radius: Math.min(4, theme.radius)
            color: root.selected === index ? theme.selection : "transparent"
            border.width: root.selected === index ? 0 : 1
            border.color: theme.border

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Math.round(14 * root.s)
                anchors.right: chevron.left
                text: modelData
                color: root.selected === index ? theme.selectionText : theme.foreground
                elide: Text.ElideRight
                font.pixelSize: Math.round(14 * root.s)
            }

            AppIcon {
                id: chevron
                anchors.right: parent.right
                anchors.rightMargin: Math.round(10 * root.s)
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(16 * root.s)
                height: width
                name: "chevron-right"
                color: root.selected === index ? theme.selectionText : theme.muted
            }

            HoverHandler {
                onHoveredChanged: if (hovered) root.selected = index
            }

            TapHandler {
                onTapped: root.chosen(index)
            }
        }
    }
}
