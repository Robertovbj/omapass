import QtQuick

// First stage: pick a database, then unlock it. Also where new KeePassXC
// databases and pass stores are created, and Bitwarden and 1Password
// accounts logged into.
FocusScope {
    id: page

    // While the scan is still running an empty list means "not found yet",
    // not "there is nothing": offering to create a database there would be
    // answering a question nobody has asked yet.
    function defaultMode() {
        return (controller.anyDatabaseFound || controller.scanning || page.unlocking) ? "list" : "confirmCreate";
    }

    property string mode: page.defaultMode()

    // The binding above is broken the first time a sheet sets `mode`, so the
    // two things it watched are followed by hand from then on. Both signals,
    // not just the scan's: whichever arrives last has to be able to correct
    // what the other one concluded on its own.
    function syncDefaultMode() {
        if (page.mode === "list" || page.mode === "confirmCreate")
            page.mode = page.defaultMode();
    }

    Connections {
        target: controller
        function onScanningChanged() { page.syncDefaultMode(); }
        function onDatabasesChanged() { page.syncDefaultMode(); }
    }

    readonly property bool unlocking: Object.keys(controller.pendingDatabase).length > 0
    readonly property bool loggingIn: controller.loginStep.length > 0

    // Ctrl+C keeps its clipboard meaning wherever text is being typed, and
    // only quits from the plain list.
    Binding {
        target: window
        property: "editingText"
        value: page.mode !== "list" || page.loggingIn || pane.typing
    }

    ListPane {
        id: pane
        anchors.fill: parent
        focus: !page.unlocking && !page.loggingIn && !controller.checkingAccount
               && page.mode === "list"
        acceptsKeys: !page.unlocking && !page.loggingIn && !controller.checkingAccount
                     && page.mode === "list"

        searchMode: false
        searchPlaceholder: i18n.t("db_app.filter_title")
        model: controller.databases
        tagFor: function(item) { return item.kind; }
        onQueryChanged: controller.databaseQuery = query

        heading: i18n.t("db_app.available_dbs_title")
        headerActions: [
            { "icon": "plus", "description": i18n.t("db_app.help_create_new_db"),
              "action": function() { page.mode = "chooseType"; } },
            { "icon": "dice", "description": i18n.t("generator.help"),
              "action": function() { page.mode = "generate"; } },
            { "icon": "settings", "description": i18n.t("settings.help"),
              "action": function() { window.settingsOpen = true; } },
            { "icon": "keyboard", "description": i18n.t("common.shortcuts_title"),
              "action": function() { page.mode = "help"; } }
        ]

        footerText: controller.hasMessage ? controller.message
                  : controller.scanning ? i18n.t("db_app.scanning")
                  : ""
        footerColor: controller.hasMessage
            ? (controller.messageIsError ? theme.error : theme.alertInfo)
            : theme.muted
        footerHint: i18n.t("common.shortcuts_hint")

        onActivated: function(index) { controller.selectDatabase(index); }
        onAddRequested: page.mode = "chooseType"
        onGenerateRequested: page.mode = "generate"
        onDeleteRequested: {
            if (controller.isBitwardenDatabase(currentIndex))
                page.mode = "confirmLogout";
            else if (controller.isOnePasswordDatabase(currentIndex))
                page.mode = "confirmLogoutOp";
        }
        onHelpRequested: page.mode = "help"
        onQuitRequested: {
            if (controller.openDatabases.length)
                controller.switchDatabase(controller.openDatabases[0].path);
            else
                Qt.quit();
        }
    }

    Modal {
        visible: page.mode === "confirmCreate"
        heading: i18n.t("common.confirm_title")
        acceptText: i18n.t("common.create")
        cancelText: i18n.t("common.no")
        cardWidth: Math.round(480 * window.s)

        onAcceptRequested: page.mode = "chooseType"
        onDismissed: Qt.quit()

        onKeyPressed: function(event) {
            switch (event.key) {
            case Qt.Key_Y:
                page.mode = "chooseType";
                break;
            case Qt.Key_N:
            case Qt.Key_Escape:
                Qt.quit();
                break;
            default:
                return;
            }
            event.accepted = true;
        }

        Text {
            width: parent.width
            text: i18n.t("db_app.no_db_found_confirm")
            color: theme.foreground
            wrapMode: Text.WordWrap
            font.pixelSize: Math.round(14 * window.s)
        }

        AppButton {
            visible: !controller.autoSearch
            width: parent.width
            text: i18n.t("db_app.enable_auto_search")
            onClicked: {
                page.mode = "list";
                controller.enableAutoSearch();
            }
        }
    }

    // Built as a list rather than written out, so a backend whose CLI is not
    // installed simply drops out without shifting what the others do.
    ChoiceModal {
        id: typeChoice
        objectName: "typeChoice"

        readonly property var choices: {
            const list = [{"label": "KeePassXC (.kdbx)", "action": "createDb"},
                          {"label": "pass", "action": "createPass"}];
            list.push({"label": "Bitwarden", "action": "bitwarden"});
            if (controller.onePasswordAvailable)
                list.push({"label": "1Password", "action": "onepassword"});
            return list;
        }

        visible: page.mode === "chooseType"
        heading: i18n.t("db_app.new_db_title")
        cardWidth: Math.round(400 * window.s)
        options: choices.map(function(choice) { return choice.label; })

        onChosen: function(index) {
            const action = typeChoice.choices[index].action;
            if (action === "bitwarden") {
                page.mode = "list";
                controller.addBitwardenAccount();
            } else if (action === "onepassword") {
                page.mode = "list";
                controller.addOnePasswordAccount();
            } else {
                page.mode = action;
            }
        }
        onDismissed: page.mode = page.defaultMode()
    }

    CreateDatabaseModal {
        visible: page.mode === "createDb"
        busy: controller.busy
        errorText: controller.unlockError

        onSubmitted: function(name, password) { controller.createKeepassDatabase(name, password); }
        onExistingSelected: function(path) { controller.openExistingKeepassDatabase(path); }
        onDismissed: page.mode = "chooseType"
    }

    CreatePassStoreModal {
        visible: page.mode === "createPass"
        busy: controller.busy
        errorText: controller.unlockError

        onSubmitted: function(directory, keyId) { controller.createPassStore(directory, keyId); }
        onExistingSelected: function(path) { controller.openExistingPassStore(path); }
        onDismissed: page.mode = "chooseType"
    }

    LoginModal {
        visible: controller.loginStep === "credentials" || controller.loginStep === "code"
                 || controller.loginStep === "deviceCode"
        step: controller.loginStep
        server: controller.loginServer
        email: controller.loginEmail
        errorText: controller.unlockError
        busy: controller.busy

        onCredentialsSubmitted: function(server, email, password) {
            controller.bitwardenLogin(server, email, password);
        }
        onCodeSubmitted: function(code) { controller.sendBitwardenCode(code); }
        onDismissed: controller.cancelBitwardenLogin()
    }

    OnePasswordLoginModal {
        visible: controller.loginStep === "opCredentials" || controller.loginStep === "opCode"
        step: controller.loginStep
        email: controller.loginEmail
        errorText: controller.unlockError
        busy: controller.busy

        onCredentialsSubmitted: function(address, email, secretKey, password, shorthand) {
            controller.onePasswordLogin(address, email, secretKey, password, shorthand);
        }
        onCodeSubmitted: function(code) { controller.sendOnePasswordCode(code); }
        onDismissed: controller.cancelOnePasswordLogin()
    }

    // Shown when the account has several two-step methods omapass can ask
    // for. The values are Bitwarden's TwoFactorProviderType.
    ChoiceModal {
        objectName: "methodChoice"
        readonly property var methods: controller.loginMethods
        readonly property var labels: ({
            0: i18n.t("bitwarden.method_authenticator"),
            1: i18n.t("bitwarden.method_email"),
            3: i18n.t("bitwarden.method_yubikey")
        })

        visible: controller.loginStep === "method"
        heading: i18n.t("bitwarden.method_title")
        status: controller.busy ? i18n.t("bitwarden.logging_in") : ""
        cardWidth: Math.round(400 * window.s)
        options: methods.map(function(method) { return labels[method]; })

        onChosen: function(index) { controller.chooseBitwardenMethod(methods[index]); }
        onDismissed: controller.cancelBitwardenLogin()
    }

    ConfirmModal {
        visible: page.mode === "confirmLogout"
        acceptText: i18n.t("common.logout")
        destructive: true
        question: i18n.t("bitwarden.logout_confirm")

        onAccepted: {
            page.mode = "list";
            controller.logoutBitwarden(pane.currentIndex);
        }
        onDismissed: page.mode = "list"
    }

    ConfirmModal {
        visible: page.mode === "confirmLogoutOp"
        acceptText: i18n.t("common.logout")
        destructive: true
        question: i18n.t("onepassword.logout_confirm")

        onAccepted: {
            page.mode = "list";
            controller.logoutOnePassword(pane.currentIndex);
        }
        onDismissed: page.mode = "list"
    }

    AccountCheckModal {
        visible: controller.checkingAccount
        accountName: controller.checkingAccountName

        onCancelled: controller.cancelAccountCheck()
    }

    UnlockModal {
        // While `op` asks for a two-step code, the login sheet is the one on
        // screen; this one comes back with the error if it fails.
        visible: page.unlocking && !page.loggingIn
        databaseName: controller.pendingDatabase.name || ""
        errorText: controller.unlockError
        busy: controller.busy
        pinAvailable: controller.pinAvailable

        onSubmitted: function(password) { controller.unlock(password); }
        onPinSubmitted: function(pin) { controller.unlockWithPin(pin); }
        onDismissed: {
            controller.cancelUnlock();
            page.mode = "list";
        }
    }

    // Generating does not need a database: a password for a site can be
    // made before deciding where (or whether) to keep it.
    GeneratorModal {
        visible: page.mode === "generate"
        onDismissed: page.mode = "list"
    }

    HelpModal {
        visible: page.mode === "help"
        sections: [
            {
                "title": i18n.t("help.nav_section"),
                "items": [["j / k", i18n.t("help.move_selection")],
                          ["g g / G", i18n.t("help.go_top_bottom")],
                          ["Ctrl+U / Ctrl+D", i18n.t("help.half_page")],
                          ["Ctrl+N / Ctrl+P", i18n.t("help.next_prev")]]
            },
            {
                "title": i18n.t("help.actions_section"),
                "items": [["Enter", i18n.t("db_app.help_select_db")],
                          ["Ctrl+A", i18n.t("db_app.help_create_new_db")],
                          ["Ctrl+G", i18n.t("generator.help")],
                          ["Ctrl+X", i18n.t("db_app.help_logout")]]
            },
            {
                "title": i18n.t("help.search_section"),
                "items": [["/ i", i18n.t("help.enter_search")],
                          ["f", i18n.t("help.click_hints")],
                          ["Esc", i18n.t("db_app.help_exit_search")]]
            },
            {
                "title": i18n.t("help.general_section"),
                "items": [["? / Ctrl+?", i18n.t("help.this_help")],
                          ["Ctrl+O", i18n.t("settings.help")],
                          ["Ctrl+Q / Ctrl+C", i18n.t("help.quit_app")]]
            }
        ]

        onDismissed: page.mode = "list"
    }

    // A database created here opens straight away, so the creation sheet
    // steps aside as soon as it succeeds.
    Connections {
        target: controller

        function onDatabaseCreated() {
            page.mode = "list";
        }
    }
}
