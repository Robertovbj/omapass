import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

// The settings that can change while omapass is running, read from and
// written back to config.toml. Theme and language are not here: they are
// read once at start-up, so changing them from the sheet would only look
// like it worked.
Modal {
    id: root

    property string wordlist: "auto"
    property string wordlistInUse: ""
    property string configPath: ""
    property bool browserSupported: false
    property bool sshAgentSetupSupported: false
    property bool sshAskpassSupported: false
    // The open vault's name when the icon boxes are its own choice; empty
    // when they edit the default.
    property string iconsVault: ""

    signal submitted(var values)

    heading: i18n.t("settings.title")
    acceptText: i18n.t("common.save")
    cardWidth: Math.round(620 * s)

    onAcceptRequested: root.submit()

    // A group's name over its settings, as Hype's overlay titles them.
    component SectionLabel: Text {
        width: parent ? parent.width : implicitWidth
        topPadding: Math.round(8 * root.s)
        color: theme.accent
        font.pixelSize: Math.round(11 * root.s)
        font.letterSpacing: 1
        font.weight: Font.DemiBold
        text: ""
    }

    // The notes under a setting.
    component Note: Text {
        width: parent ? parent.width : implicitWidth
        color: theme.muted
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(12 * root.s)
    }

    firstField: autoSearchBox

    onShown: {
        const values = controller.settings();
        autoSearchBox.checked = values.autoSearch;
        pathField.text = values.path;
        recencyBox.checked = values.recency;
        lockBox.checked = values.lockEnabled;
        lockField.text = String(values.lockMinutes);
        iconsBox.checked = values.siteIcons;
        duckDuckGoBox.checked = values.siteIconsDuckDuckGo;
        root.iconsVault = values.iconsVault || "";
        browserBox.checked = values.browserIntegration;
        root.browserSupported = values.browserSupported;
        sshAgentBox.checked = values.sshAgentSetup;
        root.sshAgentSetupSupported = values.sshAgentSetupSupported;
        sshAskpassBox.checked = values.sshAskpass;
        root.sshAskpassSupported = values.sshAskpassSupported;
        root.wordlist = values.wordlist;
        root.wordlistInUse = values.wordlistInUse;
        root.configPath = values.configPath;
        wordlistField.text = values.wordlist;
    }

    // The search path and minutes fields only show while their toggles are on;
    // moveFocus skips invisible controls automatically.
    readonly property var tabOrder: [autoSearchBox, pathField, recencyBox, lockBox, lockField,
                                     iconsBox, duckDuckGoBox, browserBox, sshAgentBox,
                                     sshAskpassBox, wordlistField]

    function focusNext(from) { root.moveFocus(root.tabOrder, from, 1); }
    function focusPrevious(from) { root.moveFocus(root.tabOrder, from, -1); }

    function submit() {
        root.submitted({
            "autoSearch": autoSearchBox.checked,
            "path": pathField.text,
            "recency": recencyBox.checked,
            "lockEnabled": lockBox.checked,
            "lockMinutes": parseInt(lockField.text) || 10,
            "wordlist": wordlistField.text,
            "siteIcons": iconsBox.checked,
            "siteIconsDuckDuckGo": duckDuckGoBox.checked,
            "browserIntegration": browserBox.checked,
            "sshAgentSetup": sshAgentBox.checked,
            "sshAskpass": sshAskpassBox.checked
        });
    }

    // Both choosers go through the desktop's portal, falling back to Qt's
    // own dialogs where there is none.
    function chooseFolder() {
        if (filePicker.available)
            filePicker.openFolder("settingsPath", i18n.t("settings.path_pick"));
        else
            folderDialog.open();
    }

    function chooseWordlist() {
        if (filePicker.available)
            filePicker.openFile("settingsWordlist", i18n.t("settings.wordlist_pick"),
                                [{ "name": i18n.t("settings.wordlist_filter"), "patterns": ["*.txt", "*.wordlist"] },
                                 { "name": i18n.t("settings.all_files"), "patterns": ["*"] }]);
        else
            wordlistDialog.open();
    }

    // Picking a file copies it into omapass' own directory, so the list
    // keeps working even if the original is moved or deleted.
    function useWordlist(file) {
        const imported = controller.importWordlist(file);
        if (imported.length > 0)
            wordlistField.text = imported;
    }

    Connections {
        target: filePicker
        enabled: root.visible

        function onPicked(purpose, path) {
            if (path.length === 0)
                return;
            if (purpose === "settingsPath")
                pathField.text = path;
            else if (purpose === "settingsWordlist")
                root.useWordlist(path);
        }
        function onFailed(purpose, error) {
            if (purpose.startsWith("settings"))
                controller.showMessage(error, true);
        }
    }

    FileDialog {
        id: wordlistDialog
        title: i18n.t("settings.wordlist_pick")
        nameFilters: [i18n.t("settings.wordlist_filter"), i18n.t("settings.all_files")]
        onAccepted: root.useWordlist(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: i18n.t("settings.path_pick")
        // A file:// URL; the setting is a plain path.
        onAccepted: pathField.text = decodeURIComponent(String(selectedFolder).replace(/^file:\/\//, ""))
    }

    Shortcut {
        sequences: ["Ctrl+F"]
        enabled: root.visible
        onActivated: root.chooseWordlist()
    }

    Shortcut {
        sequences: ["Ctrl+D"]
        enabled: root.visible && autoSearchBox.checked
        onActivated: root.chooseFolder()
    }

    SectionLabel { text: i18n.t("settings.section_general").toUpperCase() }

    CheckOption {
        id: autoSearchBox
        text: i18n.t("settings.auto_search")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(autoSearchBox)
        onPreviousRequested: root.focusPrevious(autoSearchBox)
    }

    // Typed in, or chosen in the desktop's folder chooser (also Ctrl+D).
    Item {
        visible: autoSearchBox.checked
        width: parent.width
        height: pathField.height

        Field {
            id: pathField
            width: parent.width - pathButton.width - Math.round(8 * root.s)
            label: i18n.t("settings.path_label")

            onSubmitted: root.submit()
            onCancelled: root.takeKeyboard()
            onNextRequested: root.focusNext(pathField)
            onPreviousRequested: root.focusPrevious(pathField)
        }

        AppButton {
            id: pathButton
            objectName: "pathButton"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: pathField.field.height
            text: i18n.t("settings.wordlist_choose")
            onClicked: root.chooseFolder()
        }
    }

    CheckOption {
        id: recencyBox
        text: i18n.t("settings.recency")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(recencyBox)
        onPreviousRequested: root.focusPrevious(recencyBox)
    }

    SectionLabel { text: i18n.t("settings.section_lock").toUpperCase() }

    CheckOption {
        id: lockBox
        text: i18n.t("settings.lock_enabled")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(lockBox)
        onPreviousRequested: root.focusPrevious(lockBox)
    }

    Field {
        id: lockField
        width: parent.width
        visible: lockBox.checked
        label: i18n.t("settings.lock_minutes_label")
        field.inputMethodHints: Qt.ImhDigitsOnly

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(lockField)
        onPreviousRequested: root.focusPrevious(lockField)
    }

    SectionLabel { text: i18n.t("settings.section_icons").toUpperCase() }

    CheckOption {
        id: iconsBox
        text: root.iconsVault.length > 0
            ? i18n.t("settings.site_icons_vault", { "vault": root.iconsVault })
            : i18n.t("settings.site_icons")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(iconsBox)
        onPreviousRequested: root.focusPrevious(iconsBox)
    }

    // Only once the icons are on: it is a fallback for them.
    CheckOption {
        id: duckDuckGoBox
        visible: iconsBox.checked
        text: i18n.t("settings.site_icons_duckduckgo")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(duckDuckGoBox)
        onPreviousRequested: root.focusPrevious(duckDuckGoBox)
    }

    Note {
        text: root.iconsVault.length > 0 ? i18n.t("settings.site_icons_stored")
                                         : i18n.t("settings.site_icons_default")
    }

    Note {
        visible: iconsBox.checked
        text: duckDuckGoBox.checked ? i18n.t("settings.site_icons_privacy_duckduckgo")
                                    : i18n.t("settings.site_icons_privacy")
    }

    SectionLabel {
        visible: root.browserSupported
        text: i18n.t("settings.section_browser").toUpperCase()
    }

    // Linux only for now; elsewhere there is nothing to turn on.
    CheckOption {
        id: browserBox
        visible: root.browserSupported
        text: i18n.t("settings.browser")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(browserBox)
        onPreviousRequested: root.focusPrevious(browserBox)
    }

    Note {
        visible: root.browserSupported && browserBox.checked
        text: i18n.t("settings.browser_hint")
    }

    SectionLabel {
        visible: root.sshAgentSetupSupported || root.sshAskpassSupported
        text: i18n.t("settings.section_ssh").toUpperCase()
    }

    CheckOption {
        id: sshAgentBox
        visible: root.sshAgentSetupSupported
        text: i18n.t("settings.ssh_agent")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(sshAgentBox)
        onPreviousRequested: root.focusPrevious(sshAgentBox)
    }

    Note {
        visible: root.sshAgentSetupSupported && sshAgentBox.checked
        text: i18n.t("settings.ssh_agent_hint")
    }

    CheckOption {
        id: sshAskpassBox
        visible: root.sshAskpassSupported
        text: i18n.t("settings.ssh_askpass")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(sshAskpassBox)
        onPreviousRequested: root.focusPrevious(sshAskpassBox)
    }

    Note {
        visible: root.sshAskpassSupported && sshAskpassBox.checked
        text: i18n.t("settings.ssh_askpass_hint")
    }

    SectionLabel { text: i18n.t("settings.section_generator").toUpperCase() }

    // The field and, beside it, the desktop's file chooser (also Ctrl+F).
    Item {
        width: parent.width
        height: wordlistField.height

        Field {
            id: wordlistField
            width: parent.width - wordlistButton.width - Math.round(8 * root.s)
            label: i18n.t("settings.wordlist_label")

            onSubmitted: root.submit()
            onCancelled: root.takeKeyboard()
            onNextRequested: root.focusNext(wordlistField)
            onPreviousRequested: root.focusPrevious(wordlistField)
        }

        AppButton {
            id: wordlistButton
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: wordlistField.field.height
            text: i18n.t("settings.wordlist_choose")
            onClicked: root.chooseWordlist()
        }
    }

    Note {
        text: root.wordlistInUse.length > 0
            ? i18n.t("settings.wordlist_in_use", { "path": root.wordlistInUse })
            : i18n.t("generator.no_wordlist")
        color: root.wordlistInUse.length > 0 ? theme.muted : theme.alertWarn
        wrapMode: Text.WrapAnywhere
    }

    Note {
        topPadding: Math.round(6 * root.s)
        text: i18n.t("settings.config_path", { "path": root.configPath })
        wrapMode: Text.WrapAnywhere
    }

    SectionLabel {
        visible: updater.supported
        text: i18n.t("update.section").toUpperCase()
    }

    Note {
        visible: updater.supported
        text: updater.statusText.length > 0
            ? updater.statusText
            : i18n.t("update.current_version", { "version": updater.currentVersion })
        color: updater.statusIsError ? theme.error : theme.muted
    }

    Row {
        visible: updater.supported
        spacing: Math.round(8 * root.s)

        AppButton {
            text: i18n.t("update.check")
            enabled: !updater.checking && !updater.downloading
            onClicked: updater.checkForUpdates(true)
        }

        AppButton {
            visible: updater.updateAvailable
            primary: true
            text: i18n.t("update.install_now")
            enabled: !updater.downloading
            onClicked: updater.installUpdate()
        }
    }
}
