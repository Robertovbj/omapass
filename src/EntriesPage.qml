import QtQuick
import QtQuick.Dialogs

// Second stage: the entries of the open vault, with everything that can be
// done to them.
FocusScope {
    id: page

    property string mode: "list"
    property string selectedEntry: ""
    property string iconEntry: ""
    property string iconRequest: ""

    readonly property bool menuOpen: actionsMenu.opened || databasesMenu.opened

    Binding {
        target: window
        property: "editingText"
        value: page.mode !== "list" || pane.typing
    }

    function currentEntry() {
        return controller.entryPathAt(pane.currentIndex);
    }

    function openInfo() {
        const entry = currentEntry();
        if (entry.length === 0)
            return;

        const details = controller.entryDetails(entry);
        if (Object.keys(details).length === 0)
            return;

        page.selectedEntry = entry;
        infoModal.details = details;
        page.mode = "info";
    }

    // The file dialog answers later; the entry is the one selected now.
    function chooseIcon() {
        const entry = currentEntry();
        if (entry.length === 0) {
            controller.showMessage(i18n.t("app.no_entry_selected"), true);
            return;
        }
        page.iconEntry = entry;
        page.iconRequest = "entryIcon/" + JSON.stringify([controller.openDatabases[0].path, entry]);
        if (filePicker.available)
            filePicker.openFile(page.iconRequest, i18n.t("ui.icon_pick_title"),
                                [{ "name": i18n.t("ui.icon_filter"),
                                   "patterns": ["*.png", "*.jpg", "*.jpeg", "*.ico", "*.webp", "*.gif", "*.bmp"] },
                                 { "name": i18n.t("settings.all_files"), "patterns": ["*"] }]);
        else
            iconDialog.open();
    }

    Connections {
        target: filePicker

        function onPicked(purpose, path) {
            if (purpose === page.iconRequest && path.length > 0)
                controller.chooseEntryIcon(page.iconEntry, path);
        }
        function onFailed(purpose, error) {
            if (purpose === page.iconRequest)
                controller.showMessage(error, true);
        }
    }

    FileDialog {
        id: iconDialog
        title: i18n.t("ui.icon_pick_title")
        nameFilters: [i18n.t("ui.icon_filter"), i18n.t("settings.all_files")]
        onAccepted: controller.chooseEntryIcon(page.iconEntry, selectedFile)
    }

    function openAddForm() {
        formModal.isEdit = false;
        formModal.fields = {};
        page.mode = "form";
    }

    // Editing an ordinary entry opens the form; editing an empty group opens
    // the rename sheet instead.
    function editEntry(entry) {
        if (entry.length === 0) {
            controller.showMessage(i18n.t("app.no_entry_selected"), true);
            return;
        }

        page.selectedEntry = entry;
        if (controller.isEmptyGroup(entry)) {
            const group = controller.groupNameOf(entry);
            renameModal.currentName = group.split("/").pop();
            page.mode = "rename";
        } else {
            formModal.isEdit = true;
            formModal.fields = controller.entryFields(entry);
            page.mode = "form";
        }
    }

    // Every backend can have a PIN of its own, so the shortcut works
    // wherever a database is open.
    function togglePin() {
        page.mode = controller.pinConfigured ? "pinDisable" : "pin";
    }

    // Copying or opening needs an entry under the cursor; the controller
    // says why when the entry has nothing to give.
    function withEntry(action) {
        const entry = currentEntry();
        if (entry.length === 0) {
            controller.showMessage(i18n.t("app.no_entry_selected"), true);
            return;
        }
        action(entry);
    }

    function confirmDelete() {
        const entry = currentEntry();
        if (entry.length === 0) {
            controller.showMessage(i18n.t("app.no_entry_selected"), true);
            return;
        }

        page.selectedEntry = entry;
        page.mode = "confirmDelete";
    }

    ListPane {
        id: pane
        objectName: "entriesPane"
        anchors.fill: parent
        focus: page.mode === "list" && !page.menuOpen
        acceptsKeys: page.mode === "list" && !page.menuOpen

        searchPlaceholder: i18n.t("ui.search_title")
        model: controller.entryRows
        columns: [
            { "key": "typeLabel", "title": i18n.t("ui.column_type"), "weight": 1 },
            { "key": "name", "title": i18n.t("ui.column_name"), "weight": 3 },
            { "key": "group", "title": i18n.t("ui.column_group"), "weight": 2.5, "fit": true },
            { "key": "username", "title": i18n.t("ui.column_login"), "weight": 2 },
            { "key": "url", "title": i18n.t("ui.column_url"), "weight": 2.5 }
        ]
        onQueryChanged: controller.query = query

        hasHeadingMenu: true
        onHeadingMenuRequested: function(x, y) {
            databasesMenu.anchorX = x - databasesMenu.width / 2;
            databasesMenu.anchorY = y;
            databasesMenu.selected = 0;
            databasesMenu.open();
        }
        heading: controller.vaultLabel
        headerActions: {
            const actions = [
                { "icon": "plus", "description": i18n.t("ui.help_add_entry"),
                  "action": function() { page.openAddForm(); } }
            ];
            if (controller.bitwardenBackend) {
                actions.push({ "icon": "send", "description": i18n.t("bitwarden.send_help"),
                               "action": function() { page.mode = "send"; } });
            }
            actions.push(
                { "icon": "dice", "description": i18n.t("generator.help"),
                  "action": function() { page.mode = "generate"; } },
                { "icon": "key", "description": controller.pinConfigured ? i18n.t("pin.disable_title")
                                                                         : i18n.t("pin.setup_title"),
                  "action": function() { page.togglePin(); } },
                { "icon": "settings", "description": i18n.t("settings.help"),
                  "action": function() { window.settingsOpen = true; } },
                { "icon": "lock", "description": i18n.t("ui.lock"),
                  "action": function() { controller.lock(); } }
            );
            return actions;
        }

        footerText: controller.hasMessage ? controller.message
                  : controller.busy ? i18n.t("app.working")
                  : controller.syncing ? i18n.t("bitwarden.syncing")
                  : ""
        footerColor: controller.hasMessage
            ? (controller.messageIsError ? theme.error : theme.alertInfo)
            : theme.muted
        footerHint: i18n.t("common.shortcuts_hint")

        onActivated: function(index) {
            const entry = page.currentEntry();
            if (entry.length === 0)
                return;
            if (controller.isNoteEntry(entry))
                page.openInfo();
            else
                controller.copyPassword(entry);
        }
        onDetailsRequested: page.openInfo()
        onAddRequested: page.openAddForm()
        onEditRequested: page.editEntry(page.currentEntry())
        onDeleteRequested: page.confirmDelete()
        onGenerateRequested: page.mode = "generate"
        onSendRequested: if (controller.bitwardenBackend) page.mode = "send"
        onPinToggleRequested: page.togglePin()
        onCopyUsernameRequested: page.withEntry(controller.copyUsername)
        onCopyOtpRequested: page.withEntry(controller.copyOtp)
        onCopyUrlRequested: page.withEntry(controller.copyUrl)
        onOpenUrlRequested: page.withEntry(controller.openUrl)
        onHelpRequested: page.mode = "help"
        // With a database open, ESC and q lock it and go back to the list
        // instead of quitting: leaving the app is Ctrl+Q, and stepping out
        // of the vault should not need the whole window to close.
        onQuitRequested: controller.lock()
        onMenuRequested: function(menuX, menuY) {
            actionsMenu.anchorX = menuX;
            actionsMenu.anchorY = menuY;
            actionsMenu.selected = 0;
            actionsMenu.entry = page.currentEntry();
            actionsMenu.fields = controller.entryMenuFields(actionsMenu.entry);
            actionsMenu.sshOptions = controller.sshKeyOptions(actionsMenu.entry);
            actionsMenu.open();
        }
    }

    ActionsMenu {
        id: databasesMenu
        objectName: "databasesMenu"
        parent: page
        readonly property var databases: controller.openDatabases
        options: databases.map(function(db) {
            const duplicate = databases.some(other => other.path !== db.path && other.name === db.name && other.kind === db.kind);
            return (duplicate ? db.path : db.name) + " · " + db.kind;
        }).concat([i18n.t("db_app.open_another")])
        groupStarts: [databases.length]
        onClosed: pane.claimKeyboard()
        onChosen: function(index) {
            close();
            if (index === databases.length)
                controller.chooseAnotherDatabase();
            else
                controller.switchDatabase(databases[index].path);
        }
    }

    ActionsMenu {
        id: actionsMenu
        objectName: "entryActions"
        parent: page
        property string entry: ""
        property var fields: ({})
        property var sshOptions: ({})
        readonly property var groups: {
            const entryActions = [];
            if (fields.hasPassword)
                entryActions.push("copyPassword");
            if (fields.hasUsername)
                entryActions.push("copyUsername");
            if (fields.hasUrl)
                entryActions.push("copyUrl", "openUrl");
            if (fields.hasOtp)
                entryActions.push("copyOtp");
            const result = [entryActions.concat(["edit", "delete", "info"])];
            if (Object.keys(sshOptions).length > 0)
                result.push([sshOptions.inAgent ? "removeSsh" : "addSsh", "configureSsh"]);
            result.push(["chooseIcon", "clearIcon"]);
            return result;
        }
        readonly property var actions: [].concat.apply([], groups)
        readonly property var labels: ({
            "copyPassword": i18n.t("ui.context_copy_password"),
            "copyUsername": i18n.t("ui.context_copy_username"),
            "copyUrl": i18n.t("ui.context_copy_url"),
            "openUrl": i18n.t("ui.context_open_url"),
            "copyOtp": i18n.t("ui.context_copy_otp"),
            "chooseIcon": i18n.t("ui.context_choose_icon"),
            "clearIcon": i18n.t("ui.context_clear_icon"),
            "info": i18n.t("ui.context_info"),
            "edit": i18n.t("ui.context_edit"),
            "delete": i18n.t("ui.context_delete"),
            "addSsh": i18n.t("ssh.add"),
            "removeSsh": i18n.t("ssh.remove"),
            "configureSsh": i18n.t("ssh.options")
        })
        options: actions.map(function(action) { return labels[action]; })
        groupStarts: {
            const starts = [];
            let at = 0;
            for (let i = 0; i < groups.length - 1; ++i) {
                at += groups[i].length;
                starts.push(at);
            }
            return starts;
        }

        onChosen: function(index) {
            close();
            const action = actions[index];
            if (action === "copyPassword")
                controller.copyPassword(entry);
            else if (action === "copyUsername")
                controller.copyUsername(entry);
            else if (action === "copyUrl")
                controller.copyUrl(entry);
            else if (action === "openUrl")
                controller.openUrl(entry);
            else if (action === "copyOtp")
                controller.copyOtp(entry);
            else if (action === "addSsh")
                controller.addSshKey(entry);
            else if (action === "removeSsh")
                controller.removeSshKey(entry);
            else if (action === "configureSsh") {
                page.selectedEntry = entry;
                sshModal.options = sshOptions;
                page.mode = "ssh";
            }
            else if (action === "chooseIcon")
                page.chooseIcon();
            else if (action === "clearIcon")
                page.withEntry(controller.clearEntryIcon);
            else if (action === "info")
                page.openInfo();
            else if (action === "edit")
                page.editEntry(entry);
            else if (action === "delete")
                page.confirmDelete();
        }
        onClosed: pane.claimKeyboard()
    }

    Modal {
        id: sshModal
        visible: page.mode === "ssh"
        firstField: onUnlock
        heading: i18n.t("ssh.options")
        subtitle: page.selectedEntry
        acceptText: i18n.t("common.save")
        acceptEnabled: lifetime.field.acceptableInput && Number(lifetime.text) <= 2147483647
        status: controller.hasMessage && controller.messageIsError ? controller.message : ""
        statusIsError: true

        property var options: ({})
        readonly property var tabOrder: [onUnlock, confirmation, lifetime]

        function save() {
            if (acceptEnabled && controller.saveSshKeyOptions(page.selectedEntry, onUnlock.checked,
                                                              confirmation.checked, Number(lifetime.text)))
                page.mode = "list";
        }

        onShown: {
            onUnlock.checked = options.addOnUnlock;
            confirmation.checked = options.confirm;
            lifetime.text = String(options.lifetimeSeconds);
        }
        onAcceptRequested: save()
        onDismissed: page.mode = "list"

        CheckOption {
            id: onUnlock
            text: i18n.t("ssh.on_unlock")
            onSubmitted: sshModal.save()
            onCancelled: sshModal.takeKeyboard()
            onNextRequested: sshModal.moveFocus(sshModal.tabOrder, onUnlock, 1)
            onPreviousRequested: sshModal.moveFocus(sshModal.tabOrder, onUnlock, -1)
        }

        CheckOption {
            id: confirmation
            text: i18n.t("ssh.confirm")
            onSubmitted: sshModal.save()
            onCancelled: sshModal.takeKeyboard()
            onNextRequested: sshModal.moveFocus(sshModal.tabOrder, confirmation, 1)
            onPreviousRequested: sshModal.moveFocus(sshModal.tabOrder, confirmation, -1)
        }

        Field {
            id: lifetime
            width: parent.width
            label: i18n.t("ssh.lifetime")
            field.inputMethodHints: Qt.ImhDigitsOnly
            field.validator: RegularExpressionValidator { regularExpression: /[0-9]+/ }
            onSubmitted: sshModal.save()
            onCancelled: sshModal.takeKeyboard()
            onNextRequested: sshModal.moveFocus(sshModal.tabOrder, lifetime, 1)
            onPreviousRequested: sshModal.moveFocus(sshModal.tabOrder, lifetime, -1)
        }
    }

    // Turning the PIN off asks first: it is quick to undo, but undoing it
    // means typing the master password again.
    ConfirmModal {
        objectName: "pinDisable"
        visible: page.mode === "pinDisable"
        heading: i18n.t("pin.disable_title")
        question: i18n.t("pin.disable_text")
        acceptText: i18n.t("pin.disable")
        destructive: true

        onAccepted: {
            controller.disablePin();
            page.mode = "list";
        }
        onDismissed: page.mode = "list"
    }

    PinSetupModal {
        visible: page.mode === "pin"
        busy: controller.busy

        onSubmitted: function(masterPassword, pin, allowText) {
            controller.enablePin(masterPassword, pin, allowText);
            page.mode = "list";
        }
        onDismissed: page.mode = "list"
    }

    EntryFormModal {
        id: formModal
        objectName: "entryFormModal"
        sshSupported: !controller.passBackend && !controller.onePasswordBackend && Qt.platform.os !== "windows"
        noteSupported: controller.bitwardenBackend
        status: controller.messageIsError ? controller.message : ""
        statusIsError: true
        visible: page.mode === "form"
        passBackend: controller.passBackend

        onSubmitted: function(payload) {
            if (controller.saveEntry(payload))
                page.mode = "list";
        }
        onGenerateRequested: {
            resumeDraft = true;
            page.mode = "generateForForm";
        }
        onDismissed: page.mode = "list"
    }

    // The same sheet either copies what it generated (and stays open) or
    // hands it to the form that asked for it, which stays open underneath.
    GeneratorModal {
        visible: page.mode === "generate" || page.mode === "generateForForm"
        fillMode: page.mode === "generateForForm"

        onAccepted: function(password) {
            formModal.setPassword(password);
            page.mode = "form";
        }
        onDismissed: page.mode = page.mode === "generateForForm" ? "form" : "list"
    }

    BitwardenSendModal {
        id: sendModal
        objectName: "bitwardenSendModal"
        visible: page.mode === "send"
        status: controller.messageIsError ? controller.message : ""
        statusIsError: true

        onSubmitted: function(payload) {
            if (controller.createBitwardenSend(payload))
                page.mode = "list";
        }
        onDismissed: page.mode = "list"
    }

    EntryInfoModal {
        id: infoModal
        objectName: "entryInfoModal"
        visible: page.mode === "info"
        passBackend: controller.passBackend

        onVisibleChanged: if (!visible) controller.forgetShownOtp()
        onAcceptRequested: page.editEntry(page.selectedEntry)
        onDismissed: page.mode = "list"
    }

    RenameGroupModal {
        id: renameModal
        visible: page.mode === "rename"

        onSubmitted: function(name) {
            controller.renameGroup(page.selectedEntry, name);
            page.mode = "list";
        }
        onDismissed: page.mode = "list"
    }

    ConfirmModal {
        visible: page.mode === "confirmDelete"
        acceptText: i18n.t("common.delete")
        destructive: true
        question: i18n.t("ui.confirm_delete", { "entry": page.selectedEntry })

        onAccepted: {
            controller.deleteEntry(page.selectedEntry);
            page.mode = "list";
        }
        onDismissed: page.mode = "list"
    }

    HelpModal {
        visible: page.mode === "help"
        sections: [
            {
                "title": i18n.t("help.actions_section"),
                "items": [["Enter", i18n.t("ui.help_copy_password")],
                          ["Ctrl+B", i18n.t("ui.help_copy_username")],
                          ["Ctrl+L", i18n.t("ui.help_copy_url")],
                          ["Ctrl+Shift+L", i18n.t("ui.help_open_url")],
                          ["Ctrl+T", i18n.t("ui.help_copy_otp")],
                          ["Tab", i18n.t("ui.help_view_details")],
                          [i18n.t("common.space_key"), i18n.t("ui.help_open_menu")],
                          ["Ctrl+A", i18n.t("ui.help_add_entry")],
                          ["Ctrl+E", i18n.t("ui.help_edit_entry")],
                          ["Ctrl+X", i18n.t("ui.help_delete_entry")],
                          ["Ctrl+G", i18n.t("generator.help")],
                          ["Ctrl+Shift+S", i18n.t("bitwarden.send_help")],
                          ["Ctrl+I", i18n.t("pin.help")]]
            },
            {
                "title": i18n.t("help.nav_section"),
                "items": [["j / k", i18n.t("help.move_selection")],
                          ["g g / G", i18n.t("help.go_top_bottom")],
                          ["Ctrl+U / Ctrl+D", i18n.t("help.half_page")],
                          ["Ctrl+N / Ctrl+P", i18n.t("help.next_prev")],
                          ["Ctrl+Tab", i18n.t("db_app.switch_database")]]
            },
            {
                "title": i18n.t("help.search_section"),
                "items": [["/ i", i18n.t("help.enter_search")],
                          ["f", i18n.t("help.click_hints")],
                          ["Esc", i18n.t("ui.help_exit_search")]]
            },
            {
                "title": i18n.t("ui.help_sheets_section"),
                "items": [["Enter", i18n.t("ui.help_sheet_confirm")],
                          ["Esc", i18n.t("ui.help_sheet_cancel")],
                          ["Tab / Shift+Tab", i18n.t("ui.help_sheet_fields")],
                          ["Ctrl+G", i18n.t("ui.help_sheet_generate")],
                          ["Ctrl+R", i18n.t("ui.help_sheet_regenerate")],
                          ["Ctrl+Enter", i18n.t("ui.help_sheet_save_notes")],
                          ["o", i18n.t("ui.help_sheet_open_url")],
                          ["Ctrl+D / Ctrl+F", i18n.t("ui.help_sheet_choose")],
                          ["y / n", i18n.t("ui.help_sheet_yes_no")]]
            },
            {
                "title": i18n.t("ui.help_mouse_section"),
                "items": [[i18n.t("ui.help_click_label"), i18n.t("ui.help_select_entry")],
                          [i18n.t("ui.help_double_click_label"), i18n.t("ui.help_copy_password")],
                          [i18n.t("ui.help_right_click_label"), i18n.t("ui.help_context_menu_desc")]]
            },
            {
                "title": i18n.t("help.general_section"),
                "items": [["? / Ctrl+?", i18n.t("help.this_help")],
                          ["Ctrl+O", i18n.t("settings.help")],
                          ["Esc / q", i18n.t("ui.help_lock")],
                          ["Ctrl+Q / Ctrl+C", i18n.t("help.quit_app")]]
            }
        ]

        onDismissed: page.mode = "list"
    }

    // Returning from any sheet puts the keyboard back on the list, in
    // whichever mode it was left in.
    onModeChanged: if (mode === "list") pane.claimKeyboard()
}
