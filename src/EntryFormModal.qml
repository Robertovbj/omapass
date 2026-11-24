import QtQuick
import QtQuick.Controls

// Add or edit an entry. A pass entry only exposes Group, Title and Password —
// the pass format is far less structured than KeePassXC's, and the fields it
// does not model are preserved untouched through `extra`.
Modal {
    id: root

    property bool sshSupported: false
    readonly property bool sshEntry: isEdit ? fields.type === "ssh" : sshSupported && kinds.currentIndex === 1
    readonly property bool sshAttachment: isEdit && !!fields.sshAttachment
    readonly property bool inlineSshKey: sshEntry && !sshAttachment
    property bool resumeDraft: false
    property bool isEdit: false
    property bool passBackend: false
    property var fields: ({})

    signal submitted(var payload)
    signal generateRequested()

    heading: isEdit ? i18n.t("common.edit_entry_title") : i18n.t("common.new_entry_title")
    acceptText: i18n.t("common.save")
    cardWidth: Math.round(560 * s)

    acceptEnabled: !inlineSshKey || privateKey.text.trim().length > 0

    onAcceptRequested: root.submit()

    firstField: root.isEdit ? (root.inlineSshKey ? privateKey.field : passwordField.field) : groupField.field

    onClosed: {
        privateKey.text = "";
        publicKey.text = "";
    }

    onShown: {
        if (resumeDraft) {
            resumeDraft = false;
            return;
        }
        kinds.currentIndex = 0;
        privateKey.text = inlineSshKey ? (fields.password || "") : "";
        publicKey.text = fields.sshPublicKey || "";
        groupField.text = root.fields.group || "";
        titleField.text = root.fields.title || "";
        usernameField.text = root.fields.username || "";
        passwordField.text = inlineSshKey ? "" : (root.fields.password || "");
        urlField.text = root.fields.url || "";
        extraUrlsField.text = (root.fields.extraUrls || []).join(" ");
        notesArea.text = root.fields.notes || "";
        otpField.text = root.fields.otp || "";
        refreshGroups();
    }

    function setPassword(password) {
        passwordField.text = password;
    }

    function refreshGroups() {
        groupSuggestions.model = controller.matchingGroups(groupField.text);
        groupSuggestions.currentIndex = 0;
    }

    function acceptGroupSuggestion() {
        if (groupSuggestions.model.length === 0)
            return false;

        groupField.text = groupSuggestions.model[groupSuggestions.currentIndex];
        titleField.field.forceActiveFocus();
        return true;
    }

    function submit() {
        if (!root.acceptEnabled)
            return;
        root.submitted({
            "type": root.sshEntry ? "ssh" : "login",
            "sshPublicKey": root.sshEntry ? publicKey.text : "",
            "isEdit": root.isEdit,
            "originalPath": root.fields.originalPath || "",
            "group": groupField.text,
            "title": titleField.text,
            "username": root.inlineSshKey ? "" : usernameField.text,
            "password": root.inlineSshKey ? privateKey.text : passwordField.text,
            "url": root.inlineSshKey ? "" : urlField.text,
            "extraUrls": root.inlineSshKey ? "" : extraUrlsField.text,
            "notes": notesArea.text,
            "extra": root.fields.extra || [],
            // The controller keeps the original as it was when the field was
            // not touched, and checks and normalises it when it was.
            "otp": root.inlineSshKey ? "" : otpField.text,
            "originalOtp": root.fields.otp || ""
        });
    }

    // The fields the pass backend does not show are hidden, which is how
    // moveFocus knows to skip them.
    readonly property var tabOrder: [
        kinds, groupField, titleField, privateKey, publicKey, usernameField, passwordField, revealPassword,
        otpField, urlField, extraUrlsField, notesArea
    ]

    function focusNext(from) { root.moveFocus(root.tabOrder, from, 1); }
    function focusPrevious(from) { root.moveFocus(root.tabOrder, from, -1); }

    Segmented {
        id: kinds
        objectName: "entryKinds"
        visible: !root.isEdit && root.sshSupported
        options: [i18n.t("common.password_label"), "SSH"]
        onNextRequested: root.focusNext(kinds)
        onPreviousRequested: root.focusPrevious(kinds)
        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: groupField
        width: parent.width
        label: i18n.t("common.group_label")

        onTextChanged: root.refreshGroups()
        onCancelled: root.takeKeyboard()
        onListNext: groupSuggestions.next()
        onListPrevious: groupSuggestions.previous()
        onNextRequested: root.focusNext(groupField)
        onPreviousRequested: root.focusPrevious(groupField)
        onSubmitted: if (!root.acceptGroupSuggestion()) root.submit()
    }

    SuggestionList {
        id: groupSuggestions
        width: parent.width
        onPicked: root.acceptGroupSuggestion()
    }

    Field {
        id: titleField
        width: parent.width
        label: i18n.t("common.title_label")

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(titleField)
        onPreviousRequested: root.focusPrevious(titleField)
    }

    Field {
        id: usernameField
        width: parent.width
        visible: !root.passBackend && !root.inlineSshKey
        label: i18n.t("common.username_label")

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(usernameField)
        onPreviousRequested: root.focusPrevious(usernameField)
    }

    Field {
        id: passwordField
        visible: !root.inlineSshKey
        width: parent.width
        label: i18n.t("common.password_label")
        echoMode: revealPassword.checked ? TextInput.Normal : TextInput.Password
        monospace: true

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(passwordField)
        onPreviousRequested: root.focusPrevious(passwordField)
        onGenerateRequested: root.generateRequested()
    }

    CheckOption {
        id: revealPassword
        visible: passwordField.visible
        text: i18n.t("ui.show_password")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(revealPassword)
        onPreviousRequested: root.focusPrevious(revealPassword)
    }

    // Every backend keeps one, pass included (pass-otp's line). The secret
    // a site shows when setting it up, or the otpauth:// URI behind its QR
    // code, both go in as they are.
    Field {
        id: otpField
        visible: !root.inlineSshKey
        objectName: "otpField"
        width: parent.width
        label: i18n.t("common.otp_label")
        monospace: true

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(otpField)
        onPreviousRequested: root.focusPrevious(otpField)
    }

    Field {
        id: urlField
        width: parent.width
        visible: !root.passBackend && !root.inlineSshKey
        label: i18n.t("common.url_label")

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(urlField)
        onPreviousRequested: root.focusPrevious(urlField)
    }

    // Other addresses the same login is for, as KeePassXC's additional
    // URLs: the browser offers the entry on any of them.
    Field {
        id: extraUrlsField
        objectName: "extraUrlsField"
        width: parent.width
        visible: !root.passBackend && !root.inlineSshKey
        label: i18n.t("common.extra_urls_label")
        placeholderText: "https://exemplo.com https://login.exemplo.com"

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(extraUrlsField)
        onPreviousRequested: root.focusPrevious(extraUrlsField)
    }

    component KeyField: Column {
        id: keyField
        required property string label
        property alias text: editor.text
        property alias field: editor
        property alias editorName: editor.objectName
        property alias placeholder: editor.placeholderText
        width: parent.width
        spacing: Math.round(6 * root.s)

        Text {
            text: keyField.label
            color: editor.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }
        ScrollView {
            width: parent.width
            height: Math.round(90 * root.s)
            contentWidth: availableWidth
            background: Rectangle {
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.color: editor.activeFocus ? theme.accent : theme.border
            }
            TextArea {
                id: editor
                background: null
                padding: Math.round(10 * root.s)
                font.family: systemTheme.monoFamily
                wrapMode: TextArea.WrapAnywhere
                color: theme.foreground
                placeholderTextColor: theme.muted
                selectionColor: theme.selection
                selectedTextColor: theme.selectionText
                Keys.onEscapePressed: root.takeKeyboard()
                Keys.onTabPressed: root.focusNext(keyField)
                Keys.onBacktabPressed: root.focusPrevious(keyField)
            }
        }
    }

    KeyField {
        id: privateKey
        editorName: "sshPrivateKey"
        label: i18n.t("ssh.private_label")
        placeholder: i18n.t("ssh.private_key")
        visible: root.inlineSshKey
    }

    KeyField {
        id: publicKey
        editorName: "sshPublicKey"
        label: i18n.t("ssh.public_label")
        placeholder: i18n.t("ssh.public_hint")
        visible: root.sshEntry
    }

    Column {
        width: parent.width
        visible: !root.passBackend
        spacing: Math.round(6 * root.s)

        Text {
            text: i18n.t("common.notes_label")
            color: notesArea.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        ScrollView {
            objectName: "formNotesBox"
            contentWidth: availableWidth
            width: parent.width
            height: Math.round(90 * root.s)
            Component.onCompleted: contentItem.boundsBehavior = Flickable.StopAtBounds

            background: Rectangle {
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.width: 1
                border.color: notesArea.activeFocus ? theme.accent : theme.border
            }

            TextArea {
                id: notesArea
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

                // Enter inserts a line break here, as it did in the TUI;
                // Ctrl+Enter is what saves from inside the notes.
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) {
                        root.takeKeyboard();
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Tab) {
                        root.focusNext(notesArea);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Backtab) {
                        root.focusPrevious(notesArea);
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
}
