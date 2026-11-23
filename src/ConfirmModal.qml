import QtQuick

// A yes/no question, answered with y or n exactly as in the TUI, or with the
// buttons. Enter and Esc both take the answer that loses nothing.
Modal {
    id: root

    property string question: ""

    signal accepted()

    heading: i18n.t("common.confirm_title")
    acceptText: i18n.t("common.yes")
    cancelText: i18n.t("common.no")
    cardWidth: Math.round(460 * s)

    onAcceptRequested: root.accepted()

    onKeyPressed: function(event) {
        switch (event.key) {
        case Qt.Key_Y:
            root.accepted();
            break;
        case Qt.Key_N:
        case Qt.Key_Escape:
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.dismissed();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    Text {
        width: parent.width
        text: root.question
        color: theme.foreground
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(14 * root.s)
    }
}
