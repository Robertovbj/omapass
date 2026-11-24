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
    readonly property bool passphrase: modeTabs.currentIndex === 1
    property string password: ""
    property bool passphraseAvailable: false
    // Set while the fields are being filled from the stored options, so
    // their onTextChanged does not regenerate once per field.
    property bool loading: false

    signal accepted(string password)

    heading: i18n.t("generator.title")
    acceptText: fillMode ? i18n.t("generator.use") : i18n.t("common.copy")
    acceptEnabled: root.password.length > 0
    cardWidth: Math.round(560 * s)

    onAcceptRequested: root.accepted(root.password)

    firstField: root.passphrase ? wordsField.field : lengthField.field

    onShown: {
        const options = controller.generatorOptions();
        root.passphraseAvailable = options.passphraseAvailable;

        root.loading = true;
        modeTabs.currentIndex = options.passphrase && options.passphraseAvailable ? 1 : 0;
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
    }

    function currentOptions() {
        return {
            "passphrase": root.passphrase,
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
        modeTabs, lengthField, lowerBox, upperBox, numbersBox, specialBox,
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

    // A password or a passphrase, as two tabs over the options for each.
    Segmented {
        id: modeTabs
        objectName: "generatorMode"
        visible: root.passphraseAvailable
        options: [i18n.t("generator.mode_password"), i18n.t("generator.mode_passphrase")]
        onCurrentIndexChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(modeTabs)
        onPreviousRequested: root.focusPrevious(modeTabs)
    }

    // What Enter copies (or uses), in the monospace family where l, 1 and I
    // differ, with a button to draw another.
    Rectangle {
        width: parent.width
        height: Math.max(passwordText.implicitHeight, regenerateButton.height) + Math.round(20 * root.s)
        color: theme.background
        radius: Math.min(4, theme.radius)
        border.width: 1
        border.color: theme.border

        Text {
            id: passwordText
            objectName: "generatedPassword"
            anchors.left: parent.left
            anchors.right: regenerateButton.left
            anchors.leftMargin: Math.round(14 * root.s)
            anchors.rightMargin: Math.round(8 * root.s)
            anchors.verticalCenter: parent.verticalCenter
            text: root.password
            color: theme.foreground
            wrapMode: Text.WrapAnywhere
            font.family: systemTheme.monoFamily
            font.pixelSize: Math.round(18 * root.s)
        }

        IconButton {
            id: regenerateButton
            anchors.right: parent.right
            anchors.rightMargin: Math.round(6 * root.s)
            anchors.verticalCenter: parent.verticalCenter
            iconName: "dice"
            description: i18n.t("generator.regenerate")
            onClicked: root.regenerate()
        }
    }


    Field {
        id: lengthField
        width: parent.width
        visible: !root.passphrase
        label: i18n.t("generator.length_label")
        field.inputMethodHints: Qt.ImhDigitsOnly

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(lengthField)
        onPreviousRequested: root.focusPrevious(lengthField)
        onCancelled: root.takeKeyboard()
    }

    Column {
        width: parent.width
        visible: !root.passphrase
        spacing: 0

        CheckOption {
            id: lowerBox
            text: i18n.t("generator.lower")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.takeKeyboard()
            onNextRequested: root.focusNext(lowerBox)
            onPreviousRequested: root.focusPrevious(lowerBox)
        }

        CheckOption {
            id: upperBox
            text: i18n.t("generator.upper")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.takeKeyboard()
            onNextRequested: root.focusNext(upperBox)
            onPreviousRequested: root.focusPrevious(upperBox)
        }

        CheckOption {
            id: numbersBox
            text: i18n.t("generator.numbers")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.takeKeyboard()
            onNextRequested: root.focusNext(numbersBox)
            onPreviousRequested: root.focusPrevious(numbersBox)
        }

        CheckOption {
            id: specialBox
            text: i18n.t("generator.special")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.takeKeyboard()
            onNextRequested: root.focusNext(specialBox)
            onPreviousRequested: root.focusPrevious(specialBox)
        }

        CheckOption {
            id: similarBox
            text: i18n.t("generator.exclude_similar")
            onCheckedChanged: root.regenerate()
            onSubmitted: root.accepted(root.password)
            onCancelled: root.takeKeyboard()
            onNextRequested: root.focusNext(similarBox)
            onPreviousRequested: root.focusPrevious(similarBox)
        }
    }

    Field {
        id: excludeField
        width: parent.width
        visible: !root.passphrase
        label: i18n.t("generator.exclude_label")

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(excludeField)
        onPreviousRequested: root.focusPrevious(excludeField)
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: customField
        width: parent.width
        visible: !root.passphrase
        label: i18n.t("generator.custom_label")

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(customField)
        onPreviousRequested: root.focusPrevious(customField)
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: wordsField
        width: parent.width
        visible: root.passphrase
        label: i18n.t("generator.words_label")
        field.inputMethodHints: Qt.ImhDigitsOnly

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(wordsField)
        onPreviousRequested: root.focusPrevious(wordsField)
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: separatorField
        width: parent.width
        visible: root.passphrase
        label: i18n.t("generator.separator_label")
        // A space reads as an empty field; the placeholder says which it is.
        placeholderText: i18n.t("generator.separator_space")

        onTextChanged: root.regenerate()
        onSubmitted: root.accepted(root.password)
        onNextRequested: root.focusNext(separatorField)
        onPreviousRequested: root.focusPrevious(separatorField)
        onCancelled: root.takeKeyboard()
    }
}
