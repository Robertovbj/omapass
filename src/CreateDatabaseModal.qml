import QtQuick
import QtQuick.Dialogs

// Creates a KeePassXC database in ~/.config/fpass/databases, or registers an
// existing .kdbx file chosen through the desktop portal.
Modal {
    id: root

    property string errorText: ""
    property bool busy: false

    signal submitted(string name, string password)
    signal existingSelected(string path)

    heading: i18n.t("db_app.create_db_title")
    status: errorText
    statusIsError: true
    acceptText: i18n.t("common.create")
    acceptEnabled: !busy && nameField.text.trim().length > 0

    onAcceptRequested: root.submit()

    firstField: nameField.field

    onShown: {
        nameField.text = "";
        passwordField.text = "";
    }

    function submit() {
        if (nameField.text.trim().length > 0)
            root.submitted(nameField.text, passwordField.text);
    }

    function chooseExistingFile() {
        if (filePicker.available)
            filePicker.openFile("createDbExisting", i18n.t("db_app.open_existing_kdbx_pick"),
                                [{ "name": i18n.t("db_app.kdbx_filter"), "patterns": ["*.kdbx"] },
                                 { "name": i18n.t("settings.all_files"), "patterns": ["*"] }]);
        else
            existingFileDialog.open();
    }

    Connections {
        target: filePicker
        enabled: root.visible

        function onPicked(purpose, path) {
            if (purpose === "createDbExisting" && path.length > 0)
                root.existingSelected(path);
        }
        function onFailed(purpose, error) {
            if (purpose === "createDbExisting")
                controller.showMessage(error, true);
        }
    }

    FileDialog {
        id: existingFileDialog
        title: i18n.t("db_app.open_existing_kdbx_pick")
        nameFilters: [i18n.t("db_app.kdbx_filter"), i18n.t("settings.all_files")]
        onAccepted: root.existingSelected(String(selectedFile))
    }

    Field {
        id: nameField
        width: parent.width
        label: i18n.t("db_app.filename_label")
        enabled: !root.busy

        // Enter on the name moves on to the password rather than creating a
        // database with an empty one.
        onSubmitted: passwordField.field.forceActiveFocus()
        onNextRequested: passwordField.field.forceActiveFocus()
        onPreviousRequested: passwordField.field.forceActiveFocus()
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: passwordField
        width: parent.width
        label: i18n.t("db_app.db_password_label")
        echoMode: TextInput.Password
        enabled: !root.busy

        onSubmitted: root.submit()
        onNextRequested: nameField.field.forceActiveFocus()
        onPreviousRequested: nameField.field.forceActiveFocus()
        onCancelled: root.takeKeyboard()
    }

    AppButton {
        width: parent.width
        enabled: !root.busy
        text: i18n.t("db_app.open_existing_kdbx")
        onClicked: root.chooseExistingFile()
    }
}
