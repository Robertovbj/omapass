import QtQuick
import QtQuick.Controls

// The right-click menu over the entry list. Appears at the pointer when one
// was used, and centred over the list when opened from the keyboard with
// Space — the same two ways the TUI anchored it.
Popup {
    id: root

    property var options: []
    // Indexes of the options that open a new group, set apart by a line.
    property var groupStarts: []
    property int selected: 0
    property real anchorX: -1
    property real anchorY: -1

    signal chosen(int index)

    readonly property real s: systemTheme.textScale
    readonly property real fontSize: Math.round(13 * s)
    readonly property real itemMargin: Math.round(12 * s)

    // Wide enough for the longest option: the entries are translated and the
    // list changes with the backend, so a fixed width would clip them.
    FontMetrics {
        id: metrics
        font.family: Qt.application.font.family
        font.pixelSize: root.fontSize
    }

    readonly property real optionsWidth: {
        let widest = 0;
        for (let i = 0; i < options.length; ++i)
            widest = Math.max(widest, metrics.advanceWidth(options[i]));
        return widest;
    }

    width: Math.min(Math.max(Math.round(200 * s), optionsWidth + (itemMargin + padding) * 2),
                    parent ? parent.width - Math.round(40 * s) : Math.round(420 * s))
    padding: Math.round(6 * s)
    implicitHeight: menuItems.implicitHeight + topPadding + bottomPadding
    height: parent ? Math.min(implicitHeight, parent.height) : implicitHeight
    modal: true
    focus: true
    closePolicy: Popup.CloseOnPressOutside

    x: anchorX >= 0 ? Math.min(anchorX, parent.width - width) : (parent.width - width) / 2
    y: anchorY >= 0 ? Math.min(anchorY, parent.height - height) : (parent.height - height) / 2

    onActiveFocusChanged: if (opened && !activeFocus) close()

    // Hype's menus: the panel colour, a border, the window's corners.
    background: Rectangle {
        color: theme.panel
        border.width: 1
        border.color: theme.border
        radius: theme.radius
    }

    onAboutToShow: menuView.contentY = 0

    function moveSelection(step) {
        selected = (selected + step + options.length) % options.length;
        const item = menuRows.itemAt(selected);
        if (!item)
            return;
        if (item.y < menuView.contentY)
            menuView.contentY = item.y;
        else if (item.y + item.height > menuView.contentY + menuView.height)
            menuView.contentY = item.y + item.height - menuView.height;
    }

    contentItem: Flickable {
        id: menuView
        clip: true
        contentWidth: width
        contentHeight: menuItems.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ScrollBar.vertical: AppScrollBar { view: menuView }

        Item {
            width: 0
            height: 0
            focus: true

            Keys.onPressed: function(event) {
                switch (event.key) {
                case Qt.Key_Escape:
                    root.close();
                    break;
                case Qt.Key_Down:
                case Qt.Key_J:
                    root.moveSelection(1);
                    break;
                case Qt.Key_Up:
                case Qt.Key_K:
                    root.moveSelection(-1);
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
        }

        Column {
            id: menuItems
            spacing: Math.round(2 * root.s)

            Repeater {
                id: menuRows
                model: root.options

                Column {
                    id: slot
                    required property int index
                    required property var modelData
                    readonly property bool startsGroup: root.groupStarts.indexOf(index) >= 0

                    spacing: Math.round(2 * root.s)

                    Item {
                        visible: slot.startsGroup
                        width: root.availableWidth
                        height: Math.round(9 * root.s)

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 1
                            color: theme.border
                        }
                    }

                    Rectangle {
                        width: root.availableWidth
                        height: Math.round(32 * root.s)
                        radius: Math.min(4, theme.radius)
                        color: root.selected === slot.index ? theme.hover : "transparent"

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: root.itemMargin
                            anchors.rightMargin: root.itemMargin
                            text: slot.modelData
                            color: theme.foreground
                            elide: Text.ElideRight
                            font.pixelSize: root.fontSize
                        }

                        HoverHandler {
                            onHoveredChanged: if (hovered) root.selected = slot.index
                        }

                        TapHandler {
                            onTapped: root.chosen(slot.index)
                        }
                    }
                }
            }
        }
    }
}
