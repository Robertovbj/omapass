import QtQuick
import QtQuick.Controls

// Add or edit an entry. A pass entry only exposes Group, Title and Password —
// the pass format is far less structured than KeePassXC's, and the fields it
// does not model are preserved untouched through `extra`.
Modal {
    id: root

    property bool isEdit: false
    property bool passBackend: false
    property var fields: ({})

    signal submitted(var payload)
    signal generateRequested()

    heading: isEdit ? i18n.t("common.edit_entry_title") : i18n.t("common.new_entry_title")
    hint: passBackend ? i18n.t("ui.footer_form_pass") : i18n.t("ui.footer_form_full")
    cardWidth: Math.round(560 * s)

    capturesKeys: false

    onShown: {
        groupField.text = root.fields.group || "";
        titleField.text = root.fields.title || "";
        usernameField.text = root.fields.username || "";
        passwordField.text = root.fields.password || "";
        urlField.text = root.fields.url || "";
        extraUrlsField.text = (root.fields.extraUrls || []).join(" ");
        notesArea.text = root.fields.notes || "";
        otpField.text = root.fields.otp || "";
        refreshGroups();

        // Group and title already came from the path, so editing starts on
        // the password — the field that actually changes most often.
        if (root.isEdit)
            passwordField.field.forceActiveFocus();
        else
            groupField.field.forceActiveFocus();
    }

    function setPassword(password) {
        passwordField.text = password;
        passwordField.field.forceActiveFocus();
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
        root.submitted({
            "isEdit": root.isEdit,
            "originalPath": root.fields.originalPath || "",
            "group": groupField.text,
            "title": titleField.text,
            "username": usernameField.text,
            "password": passwordField.text,
            "url": urlField.text,
            "extraUrls": extraUrlsField.text,
            "notes": notesArea.text,
            "extra": root.fields.extra || [],
            // The controller keeps the original as it was when the field was
            // not touched, and checks and normalises it when it was.
            "otp": otpField.text,
            "originalOtp": root.fields.otp || ""
        });
    }

    // The fields the pass backend does not show are hidden, which is how
    // moveFocus knows to skip them.
    readonly property var tabOrder: [
        groupField, titleField, usernameField, passwordField, revealPassword,
        otpField, urlField, extraUrlsField, notesArea
    ]

    function focusNext(from) { root.moveFocus(root.tabOrder, from, 1); }
    function focusPrevious(from) { root.moveFocus(root.tabOrder, from, -1); }

    Field {
        id: groupField
        width: parent.width
        label: i18n.t("common.group_label")

        onTextChanged: root.refreshGroups()
        onCancelled: root.dismissed()
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

        onCancelled: root.dismissed()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(titleField)
        onPreviousRequested: root.focusPrevious(titleField)
    }

    Field {
        id: usernameField
        width: parent.width
        visible: !root.passBackend
        label: i18n.t("common.username_label")

        onCancelled: root.dismissed()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(usernameField)
        onPreviousRequested: root.focusPrevious(usernameField)
    }

    Field {
        id: passwordField
        width: parent.width
        label: i18n.t("common.password_label")
        echoMode: revealPassword.checked ? TextInput.Normal : TextInput.Password

        onCancelled: root.dismissed()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(passwordField)
        onPreviousRequested: root.focusPrevious(passwordField)
        onGenerateRequested: root.generateRequested()
    }

    CheckOption {
        id: revealPassword
        text: i18n.t("ui.show_password")

        onSubmitted: root.submit()
        onCancelled: root.dismissed()
        onNextRequested: root.focusNext(revealPassword)
        onPreviousRequested: root.focusPrevious(revealPassword)
    }

    // Every backend keeps one, pass included (pass-otp's line). The secret
    // a site shows when setting it up, or the otpauth:// URI behind its QR
    // code, both go in as they are.
    Field {
        id: otpField
        objectName: "otpField"
        width: parent.width
        label: i18n.t("common.otp_label")

        onCancelled: root.dismissed()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(otpField)
        onPreviousRequested: root.focusPrevious(otpField)
    }

    Field {
        id: urlField
        width: parent.width
        visible: !root.passBackend
        label: i18n.t("common.url_label")

        onCancelled: root.dismissed()
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
        visible: !root.passBackend
        label: i18n.t("common.extra_urls_label")
        placeholderText: "https://exemplo.com https://login.exemplo.com"

        onCancelled: root.dismissed()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(extraUrlsField)
        onPreviousRequested: root.focusPrevious(extraUrlsField)
    }

    Column {
        width: parent.width
        visible: !root.passBackend
        spacing: Math.round(6 * root.s)

        Text {
            text: i18n.t("common.notes_label")
            color: notesArea.activeFocus ? theme.annotation : theme.guidance
            font.pixelSize: Math.round(12 * root.s)
        }

        ScrollView {
            width: parent.width
            height: Math.round(80 * root.s)

            TextArea {
                id: notesArea
                color: theme.base
                selectionColor: theme.selection
                selectedTextColor: theme.base
                wrapMode: TextArea.Wrap
                font.pixelSize: Math.round(14 * root.s)

                background: Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Math.max(1, Math.round(root.s))
                    color: notesArea.activeFocus ? theme.annotation : theme.guidance
                    opacity: notesArea.activeFocus ? 1.0 : 0.4
                }

                // Enter inserts a line break here, as it did in the TUI;
                // Ctrl+Enter is what saves from inside the notes.
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) {
                        root.dismissed();
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
