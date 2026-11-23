import QtQuick
import QtQuick.Controls

// Generates a password (or a diceware passphrase) in-process, which
// answers in milliseconds — so every change to the options regenerates right
// away and what you see is what you get.
//
// Opened from the entry list it copies; opened from the entry form it fills
// the password field, which is what `fillMode` switches.
Modal {
    id: root

    property bool fillMode: false
    property string password: ""
    property bool passphraseAvailable: false
    // Set while the fields are being filled from the stored options, so
    // their onTextChanged does not regenerate once per field.
    property bool loading: false

    signal accepted(string password)

    heading: i18n.t("generator.title")
    hint: fillMode ? i18n.t("generator.footer_use") : i18n.t("generator.footer_copy")
    cardWidth: Math.round(560 * s)

    capturesKeys: false

    onShown: {
        const options = controller.generatorOptions();
        root.passphraseAvailable = options.passphraseAvailable;

        root.loading = true;
        passphraseBox.checked = options.passphrase && options.passphraseAvailable;
        lengthField.text = String(options.length);
        lowerBox.checked = options.lower;
        upperBox.checked = options.upper;
        numbersBox.checked = options.numbers;
        specialBox.checked = options.special;
        similarBox.checked = options.excludeSimilar;
        excludeField.text = options.exclude;
        customField.text = options.custom;
        wordsField.text = String(options.words);
        separatorField.text = options.separator;
        root.loading = false;

        regenerate();
        lengthField.field.forceActiveFocus();
    }

    function currentOptions() {
        return {
            "passphrase": passphraseBox.checked,
            "length": parseInt(lengthField.text) || 0,
            "lower": lowerBox.checked,
            "upper": upperBox.checked,
            "numbers": numbersBox.checked,
            "special": specialBox.checked,
            "excludeSimilar": similarBox.checked,
            "exclude": excludeField.text,
            "custom": customField.text,
            "words": parseInt(wordsField.text) || 0,
            "separator": separatorField.text
        };
    }

    // Tab walks every option in reading order; the ones the current mode
    // hides (and the passphrase toggle when there is no wordlist) are
    // skipped by moveFocus.
    readonly property var tabOrder: [
        passphraseBox, lengthField, lowerBox, upperBox, numbersBox, specialBox,
        similarBox, excludeField, customField, wordsField, separatorField
    ]

    function focusNext(from) { root.moveFocus(root.tabOrder, from, 1); }
    function focusPrevious(from) { root.moveFocus(root.tabOrder, from, -1); }

    function regenerate() {
        if (root.loading)
            return;

        const result = controller.generate(currentOptions());
        root.password = result.password || "";
    }

    // Reachable wherever the focus is, which a field would otherwise swallow.
    Shortcut {
        sequences: ["Ctrl+R"]
        enabled: root.visible
        onActivated: root.regenerate()
    }

    Text {
        width: parent.width
        text: root.password
        color: theme.title
        wrapMode: Text.WrapAnywhere
        font.pixelSize: Math.round(18 * root.s)
        font.bold: true
    }

    CheckOption {
        id: passphraseBox
        visible: root.passphraseAvailable
        text: i18n.t("generator.mode_passphrase")
        onCheckedChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onCancelled: root.dismissed()
        onNextRequested: root.focusNext(passphraseBox)
        onPreviousRequested: root.focusPrevious(passphraseBox)
    }

    Field {
        id: lengthField
        width: parent.width
        visible: !passphraseBox.checked
        label: i18n.t("generator.length_label")
        field.inputMethodHints: Qt.ImhDigitsOnly

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(lengthField)
        onPreviousRequested: root.focusPrevious(lengthField)
        onCancelled: root.dismissed()
    }

    Column {
        width: parent.width
        visible: !passphraseBox.checked
        spacing: Math.round(2 * root.s)

        CheckOption {
            id: lowerBox
            text: i18n.t("generator.lower")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.dismissed()
            onNextRequested: root.focusNext(lowerBox)
            onPreviousRequested: root.focusPrevious(lowerBox)
        }

        CheckOption {
            id: upperBox
            text: i18n.t("generator.upper")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.dismissed()
            onNextRequested: root.focusNext(upperBox)
            onPreviousRequested: root.focusPrevious(upperBox)
        }

        CheckOption {
            id: numbersBox
            text: i18n.t("generator.numbers")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.dismissed()
            onNextRequested: root.focusNext(numbersBox)
            onPreviousRequested: root.focusPrevious(numbersBox)
        }

        CheckOption {
            id: specialBox
            text: i18n.t("generator.special")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.dismissed()
            onNextRequested: root.focusNext(specialBox)
            onPreviousRequested: root.focusPrevious(specialBox)
        }

        CheckOption {
            id: similarBox
            text: i18n.t("generator.exclude_similar")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.dismissed()
            onNextRequested: root.focusNext(similarBox)
            onPreviousRequested: root.focusPrevious(similarBox)
        }
    }

    Field {
        id: excludeField
        width: parent.width
        visible: !passphraseBox.checked
        label: i18n.t("generator.exclude_label")

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(excludeField)
        onPreviousRequested: root.focusPrevious(excludeField)
        onCancelled: root.dismissed()
    }

    Field {
        id: customField
        width: parent.width
        visible: !passphraseBox.checked
        label: i18n.t("generator.custom_label")

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(customField)
        onPreviousRequested: root.focusPrevious(customField)
        onCancelled: root.dismissed()
    }

    Field {
        id: wordsField
        width: parent.width
        visible: passphraseBox.checked
        label: i18n.t("generator.words_label")
        field.inputMethodHints: Qt.ImhDigitsOnly

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(wordsField)
        onPreviousRequested: root.focusPrevious(wordsField)
        onCancelled: root.dismissed()
    }

    Field {
        id: separatorField
        width: parent.width
        visible: passphraseBox.checked
        label: i18n.t("generator.separator_label")
        // A space reads as an empty field; the placeholder says which it is.
        placeholderText: i18n.t("generator.separator_space")

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(separatorField)
        onPreviousRequested: root.focusPrevious(separatorField)
        onCancelled: root.dismissed()
    }
}
