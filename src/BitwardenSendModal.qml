import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

// Creates an encrypted Bitwarden Send (text or file) and copies the shareable
// URL to the clipboard.
Modal {
    id: root

    property string filePath: ""
    property string fileRequest: "bitwardenSendFile"

    readonly property bool fileMode: modeSwitch.currentIndex === 1
    readonly property var deletionHours: [1, 24, 168, 720]

    signal submitted(var payload)

    heading: i18n.t("bitwarden.send_title")
    acceptText: i18n.t("bitwarden.send_create")
    cardWidth: Math.round(560 * s)

    acceptEnabled: nameField.text.trim().length > 0
        && (fileMode ? filePath.length > 0 : textArea.text.length > 0)

    onAcceptRequested: root.submit()

    firstField: nameField.field

    onShown: {
        modeSwitch.currentIndex = 0;
        nameField.text = "";
        textArea.text = "";
        hiddenOption.checked = false;
        filePath = "";
        deletionSwitch.currentIndex = 2; // 7 days
        maxAccessField.text = "";
        passwordField.text = "";
        notesField.text = "";
        hideEmailOption.checked = false;
    }

    function pickFile() {
        if (filePicker.available) {
            filePicker.openFile(root.fileRequest, i18n.t("bitwarden.send_choose_file_title"),
                                [{ "name": i18n.t("settings.all_files"), "patterns": ["*"] }]);
        } else {
            fileDialog.open();
        }
    }

    Connections {
        target: filePicker

        function onPicked(purpose, path) {
            if (purpose === root.fileRequest && path.length > 0)
                root.filePath = path;
        }
        function onFailed(purpose, error) {
            if (purpose === root.fileRequest)
                controller.showMessage(error, true);
        }
    }

    FileDialog {
        id: fileDialog
        title: i18n.t("bitwarden.send_choose_file_title")
        nameFilters: [i18n.t("settings.all_files")]
        onAccepted: root.filePath = selectedFile.toString()
    }

    function submit() {
        if (!root.acceptEnabled)
            return;
        root.submitted({
            "type": root.fileMode ? 1 : 0,
            "name": nameField.text,
            "text": root.fileMode ? "" : textArea.text,
            "hidden": hiddenOption.checked,
            "filePath": root.fileMode ? root.filePath : "",
            "deleteHours": root.deletionHours[deletionSwitch.currentIndex] || 168,
            "maxAccessCount": maxAccessField.text.trim().length > 0 ? Number(maxAccessField.text.trim()) : 0,
            "password": passwordField.text,
            "notes": notesField.text,
            "hideEmail": hideEmailOption.checked
        });
    }

    readonly property var tabOrder: [
        modeSwitch, nameField, textArea, hiddenOption, chooseFileButton,
        deletionSwitch, maxAccessField, passwordField, notesField, hideEmailOption
    ]

    function focusNext(from) { root.moveFocus(root.tabOrder, from, 1); }
    function focusPrevious(from) { root.moveFocus(root.tabOrder, from, -1); }

    Segmented {
        id: modeSwitch
        objectName: "sendMode"
        options: [i18n.t("bitwarden.send_type_text"), i18n.t("bitwarden.send_type_file")]
        onNextRequested: root.focusNext(modeSwitch)
        onPreviousRequested: root.focusPrevious(modeSwitch)
        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: nameField
        objectName: "sendNameField"
        width: parent.width
        label: i18n.t("bitwarden.send_name_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(nameField)
        onPreviousRequested: root.focusPrevious(nameField)
    }

    Column {
        width: parent.width
        visible: !root.fileMode
        spacing: Math.round(6 * root.s)

        Text {
            text: i18n.t("bitwarden.send_text_label")
            color: textArea.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        ScrollView {
            contentWidth: availableWidth
            width: parent.width
            height: Math.round(110 * root.s)
            Component.onCompleted: contentItem.boundsBehavior = Flickable.StopAtBounds

            background: Rectangle {
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.width: 1
                border.color: textArea.activeFocus ? theme.accent : theme.border
            }

            TextArea {
                id: textArea
                objectName: "sendTextArea"
                color: theme.foreground
                selectionColor: theme.selection
                selectedTextColor: theme.selectionText
                wrapMode: TextArea.Wrap
                font.pixelSize: Math.round(14 * root.s)
                leftPadding: Math.round(12 * root.s)
                rightPadding: Math.round(12 * root.s)
                topPadding: Math.round(9 * root.s)
                bottomPadding: Math.round(9 * root.s)
                background: null

                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) {
                        root.takeKeyboard();
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Tab) {
                        root.focusNext(textArea);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Backtab) {
                        root.focusPrevious(textArea);
                        event.accepted = true;
                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                               && (event.modifiers & Qt.ControlModifier)) {
                        root.submit();
                        event.accepted = true;
                    }
                }
            }
        }
    }

    CheckOption {
        id: hiddenOption
        visible: !root.fileMode
        text: i18n.t("bitwarden.send_hidden_label")
        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(hiddenOption)
        onPreviousRequested: root.focusPrevious(hiddenOption)
    }

    Column {
        width: parent.width
        visible: root.fileMode
        spacing: Math.round(6 * root.s)

        Text {
            text: i18n.t("bitwarden.send_file_label")
            color: chooseFileButton.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        Row {
            width: parent.width
            spacing: Math.round(8 * root.s)

            Rectangle {
                width: Math.max(0, parent.width - chooseFileButton.width - parent.spacing)
                height: chooseFileButton.height
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.width: 1
                border.color: theme.border

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: Math.round(10 * root.s)
                    anchors.rightMargin: Math.round(10 * root.s)
                    verticalAlignment: Text.AlignVCenter
                    text: root.filePath.length > 0 ? root.filePath : i18n.t("bitwarden.send_no_file")
                    color: root.filePath.length > 0 ? theme.foreground : theme.muted
                    elide: Text.ElideMiddle
                    font.pixelSize: Math.round(13 * root.s)
                }
            }

            AppButton {
                id: chooseFileButton
                objectName: "sendChooseFileButton"
                text: i18n.t("bitwarden.send_choose_file")
                onClicked: root.pickFile()
                Keys.onTabPressed: root.focusNext(chooseFileButton)
                Keys.onBacktabPressed: root.focusPrevious(chooseFileButton)
                Keys.onEscapePressed: root.takeKeyboard()
            }
        }
    }

    Column {
        width: parent.width
        spacing: Math.round(6 * root.s)

        Text {
            text: i18n.t("bitwarden.send_delete_label")
            color: deletionSwitch.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        Segmented {
            id: deletionSwitch
            options: [
                i18n.t("bitwarden.send_delete_1h"),
                i18n.t("bitwarden.send_delete_1d"),
                i18n.t("bitwarden.send_delete_7d"),
                i18n.t("bitwarden.send_delete_30d")
            ]
            onNextRequested: root.focusNext(deletionSwitch)
            onPreviousRequested: root.focusPrevious(deletionSwitch)
            onSubmitted: root.submit()
            onCancelled: root.takeKeyboard()
        }
    }

    Field {
        id: maxAccessField
        width: parent.width
        label: i18n.t("bitwarden.send_max_access_label")
        placeholderText: i18n.t("bitwarden.send_optional_hint")
        field.inputMethodHints: Qt.ImhDigitsOnly
        field.validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(maxAccessField)
        onPreviousRequested: root.focusPrevious(maxAccessField)
    }

    Field {
        id: passwordField
        width: parent.width
        label: i18n.t("bitwarden.send_password_label")
        placeholderText: i18n.t("bitwarden.send_optional_hint")
        echoMode: TextInput.Password
        monospace: true
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(passwordField)
        onPreviousRequested: root.focusPrevious(passwordField)
    }

    Field {
        id: notesField
        width: parent.width
        label: i18n.t("bitwarden.send_notes_label")
        placeholderText: i18n.t("bitwarden.send_optional_hint")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(notesField)
        onPreviousRequested: root.focusPrevious(notesField)
    }

    CheckOption {
        id: hideEmailOption
        text: i18n.t("bitwarden.send_hide_email")
        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(hideEmailOption)
        onPreviousRequested: root.focusPrevious(hideEmailOption)
    }
}
