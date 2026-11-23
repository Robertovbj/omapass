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
    readonly property bool hasExtraUrls: (root.details.extraUrls || []).length > 0

    // The Tab chain through the additional URLs, which come and go with the
    // entry shown.
    function extraUrlView(index) {
        const field = extraUrlViews.itemAt(index);
        return field ? field.view : null;
    }
    function fieldAfterUrls() {
        return root.hasOtp ? otpView.view : notesView.view;
    }
    function lastUrlView() {
        return extraUrlViews.count > 0 ? root.extraUrlView(extraUrlViews.count - 1) : urlView.view;
    }
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

    heading: root.details.title || i18n.t("common.entry_info_title")
    subtitle: i18n.t("common.entry_info_title")
    cancelOnly: true
    cancelText: i18n.t("common.close")
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
        // What `o` does on a field holding an address: open it.
        property var openAction: null
        property bool selectable: true
        // A secret (the one-time code): monospace and larger.
        property bool monospace: false

        width: parent.width
        spacing: Math.round(6 * root.s)

        Text {
            text: fieldColumn.label
            color: text.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        // The field Tab is on is boxed in the accent, so Enter's target is
        // plain to see.
        ScrollView {
            id: valueBox
            width: parent.width
            height: Math.round((fieldColumn.monospace ? 26 : 22) * root.s) * fieldColumn.rows
                    + topPadding + bottomPadding
            padding: Math.round(8 * root.s)
            leftPadding: Math.round(12 * root.s)
            rightPadding: Math.round(12 * root.s)

            Component.onCompleted: contentItem.boundsBehavior = Flickable.StopAtBounds

            background: Rectangle {
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.width: 1
                border.color: text.activeFocus ? theme.accent : theme.border
            }

            TextEdit {
                id: text
                width: valueBox.availableWidth
                text: fieldColumn.value
                color: theme.foreground
                readOnly: true
                selectByMouse: fieldColumn.selectable
                selectionColor: theme.selection
                selectedTextColor: theme.selectionText
                wrapMode: TextEdit.Wrap
                font.pixelSize: Math.round((fieldColumn.monospace ? 18 : 14) * root.s)
                font.family: fieldColumn.monospace ? systemTheme.monoFamily : Qt.application.font.family
                font.letterSpacing: fieldColumn.monospace ? 2 : 0

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
                    case Qt.Key_O:
                        if (!fieldColumn.openAction)
                            return;
                        fieldColumn.openAction();
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
        openAction: root.details.url ? function() { controller.openAddress(root.details.url); } : null
        nextField: root.hasExtraUrls ? root.extraUrlView(0) : root.fieldAfterUrls()
        previousField: usernameView.view
    }

    // One field per additional URL, each copied with Enter and opened with o
    // like the main one.
    Repeater {
        id: extraUrlViews
        model: root.passBackend ? [] : (root.details.extraUrls || [])

        InfoField {
            required property string modelData
            required property int index

            objectName: "extraUrlField" + index
            label: i18n.t("ui.info_extra_url", { "n": index + 1 })
            value: modelData
            openAction: function() { controller.openAddress(modelData); }
            nextField: index + 1 < extraUrlViews.count ? root.extraUrlView(index + 1) : root.fieldAfterUrls()
            previousField: index > 0 ? root.extraUrlView(index - 1) : urlView.view
        }
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
            monospace: true
            copyAction: controller.copyShownOtp
            nextField: root.passBackend ? titleView.view : notesView.view
            previousField: root.passBackend ? titleView.view : root.lastUrlView()
        }

        // How much of the period is left, emptying towards the next code.
        Rectangle {
            width: parent.width
            height: Math.max(3, Math.round(3 * root.s))
            radius: height / 2
            color: theme.border

            Rectangle {
                height: parent.height
                radius: parent.radius
                width: root.otp.period ? parent.width * root.otp.secondsLeft / root.otp.period : 0
                // The last few seconds in the warning colour: a code copied
                // then may be stale by the time it is pasted.
                color: (root.otp.secondsLeft || 0) <= 5 ? theme.alertWarn : theme.accent
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
        previousField: root.hasOtp ? otpView.view : root.lastUrlView()
    }
}
