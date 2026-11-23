import QtQuick

// Turns PIN unlock on for the open database: its password is kept in the
// system keyring, encrypted with a key derived from the PIN, so the password
// has to be typed once here. A short PIN is accepted, but the warning below
// says in as many words how little it protects — and the toggle opens the
// PIN up to letters and symbols, which widens the alphabet a guesser has to
// work through.
Modal {
    id: root

    property bool busy: false

    signal submitted(string masterPassword, string pin, bool allowText)

    readonly property string warning: controller.pinWeakWarning(pinField.text)

    heading: i18n.t("pin.setup_title")
    hint: i18n.t("pin.setup_footer")
    cardWidth: Math.round(480 * s)

    capturesKeys: false

    onShown: {
        revealPassword.checked = false;
        revealPin.checked = false;
        allowText.checked = false;
        passwordField.text = "";
        pinField.text = "";
        confirmField.text = "";
        passwordField.field.forceActiveFocus();
    }

    readonly property var tabOrder: [
        passwordField, revealPassword, pinField, confirmField, revealPin, allowText
    ]

    function focusNext(from) { root.moveFocus(root.tabOrder, from, 1); }
    function focusPrevious(from) { root.moveFocus(root.tabOrder, from, -1); }

    function submit() {
        if (passwordField.text.length === 0) {
            passwordField.field.forceActiveFocus();
            return;
        }
        const invalid = controller.validatePin(pinField.text, confirmField.text,
                                               allowText.checked);
        if (invalid.length > 0) {
            controller.showMessage(invalid, true);
            pinField.field.forceActiveFocus();
            return;
        }
        root.submitted(passwordField.text, pinField.text, allowText.checked);
    }

    Field {
        id: passwordField
        width: parent.width
        label: i18n.t("common.password_label")
        echoMode: revealPassword.checked ? TextInput.Normal : TextInput.Password
        enabled: !root.busy

        onSubmitted: pinField.field.forceActiveFocus()
        onNextRequested: root.focusNext(passwordField)
        onPreviousRequested: root.focusPrevious(passwordField)
        onCancelled: root.dismissed()
    }

    CheckOption {
        id: revealPassword
        text: i18n.t("ui.show_password")
        enabled: !root.busy

        onSubmitted: root.submit()
        onCancelled: root.dismissed()
        onNextRequested: root.focusNext(revealPassword)
        onPreviousRequested: root.focusPrevious(revealPassword)
    }

    Field {
        id: pinField
        width: parent.width
        label: i18n.t("pin.label")
        echoMode: revealPin.checked ? TextInput.Normal : TextInput.Password
        field.inputMethodHints: allowText.checked ? Qt.ImhNone : Qt.ImhDigitsOnly
        enabled: !root.busy

        onSubmitted: confirmField.field.forceActiveFocus()
        onNextRequested: root.focusNext(pinField)
        onPreviousRequested: root.focusPrevious(pinField)
        onCancelled: root.dismissed()
    }

    Field {
        id: confirmField
        width: parent.width
        label: i18n.t("pin.confirm_label")
        echoMode: revealPin.checked ? TextInput.Normal : TextInput.Password
        field.inputMethodHints: allowText.checked ? Qt.ImhNone : Qt.ImhDigitsOnly
        enabled: !root.busy

        onSubmitted: root.submit()
        onNextRequested: root.focusNext(confirmField)
        onPreviousRequested: root.focusPrevious(confirmField)
        onCancelled: root.dismissed()
    }

    CheckOption {
        id: revealPin
        text: i18n.t("pin.show")
        enabled: !root.busy

        onSubmitted: root.submit()
        onCancelled: root.dismissed()
        onNextRequested: root.focusNext(revealPin)
        onPreviousRequested: root.focusPrevious(revealPin)
    }

    CheckOption {
        id: allowText
        text: i18n.t("pin.allow_text")
        enabled: !root.busy

        onSubmitted: root.submit()
        onCancelled: root.dismissed()
        onNextRequested: root.focusNext(allowText)
        onPreviousRequested: root.focusPrevious(allowText)
    }

    Text {
        width: parent.width
        visible: root.warning.length > 0
        text: root.warning
        color: theme.alertWarn
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(12 * root.s)
    }
}
