import QtQuick
import QtQuick.Controls

// Read-only view of an entry, opened with Tab. Selecting text copies it, the
// way dragging over a field did in the TUI; Enter copies the whole field
// under the cursor, and Tab cycles between the fields.
//
// A pass entry only shows the Title — the format has no URL or Notes of its
// own. An entry with a one-time password, in any backend, also shows its
// current code and how long it has left; the secret stays in the controller.
Modal {
    id: root

    property bool passBackend: false
    property var details: ({})
    readonly property bool hasOtp: !!root.details.hasOtp
    property var otp: ({})

    // Read from `details` itself: in onDetailsChanged the hasOtp binding may
    // not have caught up yet.
    function refreshOtp() {
        root.otp = root.details.hasOtp ? controller.otpNow() : ({});
    }

    onDetailsChanged: refreshOtp()

    // Every second rather than once per period: the countdown is part of
    // what the code is worth, and a period can start at any second.
    Timer {
        interval: 1000
        repeat: true
        running: root.visible && root.hasOtp
        onTriggered: root.refreshOtp()
    }

    heading: i18n.t("common.entry_info_title")
    hint: passBackend ? i18n.t("ui.info_footer_pass") : i18n.t("ui.info_footer_full")
    cardWidth: Math.round(600 * s)

    onKeyPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Q) {
            root.dismissed();
            event.accepted = true;
        }
    }

    capturesKeys: false

    onShown: {
        refreshOtp();
        titleView.view.forceActiveFocus();
    }

    component InfoField: Column {
        id: fieldColumn

        property string label: ""
        property string value: ""
        property int rows: 1
        property Item nextField: null
        property Item previousField: null
        property alias view: text
        // What Enter does instead of copying the text as it stands: the
        // one-time code goes through the clipboard clearing of a password.
        property var copyAction: null
        property bool selectable: true

        width: parent.width
        spacing: Math.round(6 * root.s)

        Text {
            text: fieldColumn.label
            color: text.activeFocus ? theme.annotation : theme.guidance
            font.pixelSize: Math.round(12 * root.s)
        }

        ScrollView {
            width: parent.width
            height: Math.round(22 * root.s) * fieldColumn.rows

            TextEdit {
                id: text
                width: parent.width
                text: fieldColumn.value
                color: theme.base
                readOnly: true
                selectByMouse: fieldColumn.selectable
                selectionColor: theme.annotation
                selectedTextColor: theme.background
                wrapMode: TextEdit.Wrap
                font.pixelSize: Math.round(14 * root.s)

                // Copying once the selection settles reproduces the TUI's
                // select-to-copy without firing on every pixel of the drag.
                onSelectedTextChanged: copyTimer.restart()

                Timer {
                    id: copyTimer
                    interval: 250
                    onTriggered: controller.copyText(text.selectedText)
                }

                Keys.onPressed: function(event) {
                    switch (event.key) {
                    case Qt.Key_Escape:
                    case Qt.Key_Q:
                        root.dismissed();
                        break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        if (fieldColumn.copyAction)
                            fieldColumn.copyAction();
                        else
                            controller.copyText(fieldColumn.value);
                        break;
                    case Qt.Key_Tab:
                        if (fieldColumn.nextField)
                            fieldColumn.nextField.forceActiveFocus();
                        break;
                    case Qt.Key_Backtab:
                        if (fieldColumn.previousField)
                            fieldColumn.previousField.forceActiveFocus();
                        break;
                    default:
                        return;
                    }
                    event.accepted = true;
                }
            }
        }
    }

    InfoField {
        id: titleView
        label: i18n.t("common.title_word")
        value: root.details.title || ""
        nextField: root.passBackend ? (root.hasOtp ? otpView.view : titleView.view) : usernameView.view
        previousField: root.passBackend ? (root.hasOtp ? otpView.view : titleView.view) : notesView.view
    }

    InfoField {
        id: usernameView
        objectName: "usernameField"
        visible: !root.passBackend
        label: i18n.t("ui.info_login")
        value: root.details.username || ""
        nextField: urlView.view
        previousField: titleView.view
    }

    InfoField {
        id: urlView
        visible: !root.passBackend
        label: "URL"
        value: root.details.url || ""
        nextField: root.hasOtp ? otpView.view : notesView.view
        previousField: usernameView.view
    }

    Column {
        width: parent.width
        visible: root.hasOtp
        spacing: Math.round(4 * root.s)

        InfoField {
            id: otpView
            objectName: "otpField"
            label: i18n.t("ui.info_otp") + "  ·  "
                   + i18n.t("ui.info_otp_seconds", { "seconds": root.otp.secondsLeft || 0 })
            value: root.otp.code || ""
            selectable: false
            copyAction: controller.copyShownOtp
            nextField: root.passBackend ? titleView.view : notesView.view
            previousField: root.passBackend ? titleView.view : urlView.view
        }

        // How much of the period is left, emptying towards the next code.
        Rectangle {
            width: parent.width
            height: Math.max(2, Math.round(2 * root.s))
            radius: height / 2
            color: theme.guidance
            opacity: 0.25

            Rectangle {
                height: parent.height
                radius: parent.radius
                width: root.otp.period ? parent.width * root.otp.secondsLeft / root.otp.period : 0
                // The last few seconds in the warning colour: a code copied
                // then may be stale by the time it is pasted.
                color: (root.otp.secondsLeft || 0) <= 5 ? theme.alertWarn : theme.annotation
            }
        }
    }

    InfoField {
        id: notesView
        visible: !root.passBackend
        label: i18n.t("common.notes_label")
        value: root.details.notes || ""
        rows: 5
        nextField: titleView.view
        previousField: root.hasOtp ? otpView.view : urlView.view
    }
}
