import QtQuick
import QtQuick.Controls

// Shared frame for every sheet omapass puts over the list, in the shape of
// Hype's dialogs: a title, a muted line under it, the body, and Cancel
// beside the main action. A status line above the buttons says what is
// happening (logging in, a wrong password).
//
// Visibility stays owned by the page (bound to its current mode), so this
// never closes itself: clicking the dimmed backdrop, pressing keys and the
// buttons are reported as signals for the page to act on. The buttons are for
// the mouse; the keyboard keeps each sheet's own keys (Enter, Esc, y/n), and
// every shortcut is listed under Ctrl+?.
Popup {
    id: root

    property string heading: ""
    property string subtitle: ""
    property string status: ""
    property bool statusIsError: false
    // The main action's caption; without one the sheet has no buttons.
    property string acceptText: ""
    property bool acceptEnabled: true
    // The main action destroys something (delete, log out): it is drawn red.
    property bool destructive: false
    property string cancelText: i18n.t("common.cancel")
    property bool showCancel: true
    // Only the way out, for a sheet with nothing to confirm (a choice made
    // by picking a row, a wait).
    property bool cancelOnly: false
    property real cardWidth: Math.round(460 * root.s)

    // Sheets made only of buttons and text rely on this; ones with fields
    // turn it off and let the focused field handle its own keys, otherwise
    // the catcher below takes the focus the field needs to be typed into.
    property bool capturesKeys: true
    signal keyPressed(var event)
    signal dismissed()
    // The main action's button was clicked.
    signal acceptRequested()

    // Popup only emits opened() after its enter transition, and a sheet whose
    // visibility is bound to the page's mode can be shown before that ever
    // runs. `shown` fires once per appearance whichever path got us here, so
    // sheets have a dependable moment to reset fields and take the keyboard.
    signal shown()

    property bool announcedShown: false

    onVisibleChanged: {
        if (!visible)
            announcedShown = false;
        else if (!announcedShown)
            announceShown();
    }

    onOpened: if (!announcedShown) announceShown()

    // Puts the keyboard on the sheet's own key handling, for a sheet that
    // opens over something that may have taken it meanwhile.
    function takeKeyboard() {
        keyCatcher.forceActiveFocus();
    }

    function announceShown() {
        announcedShown = true;
        shown();
    }

    // The Tab chain of a sheet with fields: moves the keyboard `step` places
    // along `order` from `from`, wrapping around and skipping whatever is
    // hidden or disabled at the moment. A Field is focused through its
    // inner text field; anything else (a CheckOption, a TextArea) directly.
    function moveFocus(order, from, step) {
        const live = order.filter(function(item) { return item.visible && item.enabled; });
        if (live.length === 0)
            return;

        let index = live.indexOf(from);
        if (index < 0)
            index = step > 0 ? -1 : 0;
        const target = live[(index + step + live.length) % live.length];
        (target.field || target).forceActiveFocus();
    }

    default property alias body: bodyColumn.data

    readonly property real s: systemTheme.textScale
    readonly property real gap: Math.round(18 * s)
    readonly property bool hasButtons: acceptText.length > 0 || cancelOnly

    anchors.centerIn: Overlay.overlay
    width: Math.min(cardWidth, parent ? parent.width - Math.round(40 * s) : cardWidth)
    modal: true
    focus: true
    padding: Math.round(24 * s)
    closePolicy: Popup.NoAutoClose

    // A tall sheet (the full entry form, say) is capped to the window and
    // scrolls its body rather than growing past the edges and losing its
    // title and buttons off-screen.
    implicitHeight: topPadding + bottomPadding + titleBlock.implicitHeight
        + bodyColumn.implicitHeight + (bodyColumn.implicitHeight > 4 ? root.gap : 0)
        + footer.implicitHeight + (footer.implicitHeight > 0 ? root.gap : 0)
    height: Math.min(implicitHeight,
                     parent ? parent.height - Math.round(40 * s) : implicitHeight)

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)

        // Only a tap that really lands outside the card. The backdrop sees
        // taps on the card too: picking a row in a ChoiceModal swaps it for
        // the next sheet, and the same tap read here as "outside" would
        // dismiss that choice straight back to the list.
        TapHandler {
            onTapped: function(eventPoint) {
                if (!root.visible)
                    return;
                const card = root.background;
                const inside = card.mapFromItem(null, eventPoint.scenePosition.x,
                                                eventPoint.scenePosition.y);
                if (!card.contains(inside))
                    root.dismissed();
            }
        }
    }

    background: Rectangle {
        color: theme.panel
        radius: theme.radius
        border.width: 1
        border.color: theme.border
    }

    contentItem: Item {
        Item {
            id: keyCatcher
            width: 0
            height: 0
            focus: root.capturesKeys

            Keys.onPressed: function(event) {
                root.keyPressed(event);
            }
        }

        Column {
            id: titleBlock
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Math.round(6 * root.s)

            Text {
                objectName: "modalTitle"
                width: parent.width
                visible: text.length > 0
                text: root.heading.trim()
                color: theme.foreground
                elide: Text.ElideMiddle
                font.pixelSize: Math.round(20 * root.s)
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                visible: text.length > 0
                text: root.subtitle
                color: theme.muted
                wrapMode: Text.WordWrap
                font.pixelSize: Math.round(14 * root.s)
            }
        }

        ScrollView {
            id: bodyArea
            objectName: "modalBody"
            anchors.top: titleBlock.bottom
            anchors.bottom: footer.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: bodyColumn.implicitHeight > 4 ? root.gap : 0
            anchors.bottomMargin: footer.implicitHeight > 0 ? root.gap : 0
            contentWidth: availableWidth
            // At an edge the body stops, rather than going past and coming
            // back (Omajim's dialogs do the same).
            Component.onCompleted: contentItem.boundsBehavior = Flickable.StopAtBounds

            ScrollBar.vertical: AppScrollBar {
                parent: bodyArea
                x: bodyArea.mirrored ? 0 : bodyArea.width - width
                y: bodyArea.topPadding
                height: bodyArea.availableHeight
                view: bodyArea.contentItem
            }

            // A little room inside the scrolled area, which clips: a row's
            // or a field's border sitting right on its edge would be cut.
            Column {
                id: bodyColumn
                width: bodyArea.availableWidth
                topPadding: 2
                bottomPadding: 2
                spacing: Math.round(12 * root.s)
            }
        }

        // The wheel and the touchpad move the body as they move Omajim's
        // lists: a notch goes a good way, and further when turned fast.
        SmoothWheel {
            view: bodyArea.contentItem
        }

        Column {
            id: footer
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Math.round(12 * root.s)

            Text {
                objectName: "modalStatus"
                width: parent.width
                visible: text.length > 0
                text: root.status
                color: root.statusIsError ? theme.error : theme.muted
                wrapMode: Text.WordWrap
                font.pixelSize: Math.round(13 * root.s)
            }

            Row {
                anchors.right: parent.right
                visible: root.hasButtons
                spacing: Math.round(10 * root.s)
                layoutDirection: Qt.LeftToRight

                AppButton {
                    objectName: "modalCancel"
                    visible: root.showCancel || root.cancelOnly
                    text: root.cancelText
                    onClicked: root.dismissed()
                }

                AppButton {
                    objectName: "modalAccept"
                    visible: root.acceptText.length > 0
                    primary: true
                    destructive: root.destructive
                    text: root.acceptText
                    enabled: root.acceptEnabled
                    onClicked: root.acceptRequested()
                }
            }
        }
    }
}
