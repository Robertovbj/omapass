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
    acceptText: i18n.t("pin.enable")
    acceptEnabled: !busy
    cardWidth: Math.round(480 * s)

    onAcceptRequested: root.submit()

    firstField: passwordField.field

    onShown: {
        revealPassword.checked = false;
        revealPin.checked = false;
        allowText.checked = false;
        passwordField.text = "";
        pinField.text = "";
        confirmField.text = "";
    }

    // Only digits go in while letters and symbols are not allowed.
    RegularExpressionValidator {
        id: digitsOnly
        regularExpression: /[0-9]*/
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
        onCancelled: root.takeKeyboard()
    }

    CheckOption {
        id: revealPassword
        text: i18n.t("ui.show_password")
        enabled: !root.busy

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(revealPassword)
        onPreviousRequested: root.focusPrevious(revealPassword)
    }

    Field {
        id: pinField
        objectName: "pinField"
        width: parent.width
        label: i18n.t("pin.label")
        echoMode: revealPin.checked ? TextInput.Normal : TextInput.Password
        field.inputMethodHints: allowText.checked ? Qt.ImhNone : Qt.ImhDigitsOnly
        field.validator: allowText.checked ? null : digitsOnly
        enabled: !root.busy

        onSubmitted: confirmField.field.forceActiveFocus()
        onNextRequested: root.focusNext(pinField)
        onPreviousRequested: root.focusPrevious(pinField)
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: confirmField
        width: parent.width
        label: i18n.t("pin.confirm_label")
        echoMode: revealPin.checked ? TextInput.Normal : TextInput.Password
        field.inputMethodHints: allowText.checked ? Qt.ImhNone : Qt.ImhDigitsOnly
        field.validator: allowText.checked ? null : digitsOnly
        enabled: !root.busy

        onSubmitted: root.submit()
        onNextRequested: root.focusNext(confirmField)
        onPreviousRequested: root.focusPrevious(confirmField)
        onCancelled: root.takeKeyboard()
    }

    CheckOption {
        id: revealPin
        text: i18n.t("pin.show")
        enabled: !root.busy

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(revealPin)
        onPreviousRequested: root.focusPrevious(revealPin)
    }

    CheckOption {
        id: allowText
        objectName: "pinAllowText"
        text: i18n.t("pin.allow_text")
        enabled: !root.busy

        // Back to digits only: whatever else was typed goes, rather than
        // sitting in the boxes where the validator can no longer be met.
        onCheckedChanged: {
            if (!checked) {
                pinField.text = pinField.text.replace(/[^0-9]/g, "");
                confirmField.text = confirmField.text.replace(/[^0-9]/g, "");
            }
        }

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
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
