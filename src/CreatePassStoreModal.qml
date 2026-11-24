import QtQuick
import QtQuick.Dialogs

// Initialises a new pass store, or registers an existing pass directory chosen
// through the desktop portal. omapass never generates GPG keys.
Modal {
    id: root

    property string errorText: ""
    property bool busy: false
    property var keys: []

    signal submitted(string directory, string keyId)
    signal existingSelected(string path)

    heading: i18n.t("db_app.new_pass_store_title")
    status: errorText
    statusIsError: true
    acceptText: i18n.t("common.create")
    acceptEnabled: !busy && root.keys.length > 0

    onAcceptRequested: root.submit()
    cardWidth: Math.round(560 * s)

    firstField: directoryField.field

    onShown: {
        root.keys = controller.gpgKeys();
        directoryField.text = controller.defaultPassStoreDirectory();
        refreshSuggestions();
        keys.currentIndex = 0;
    }

    function refreshSuggestions() {
        suggestions.model = controller.directorySuggestions(directoryField.text);
        suggestions.currentIndex = 0;
    }

    // Completes the highlighted folder and leaves the trailing slash in
    // place, so the next segment can be completed straight away.
    function acceptSuggestion() {
        if (suggestions.model.length === 0)
            return false;

        directoryField.text = suggestions.model[suggestions.currentIndex] + "/";
        refreshSuggestions();
        return true;
    }

    function submit() {
        if (root.keys.length === 0)
            return;
        root.submitted(directoryField.text, root.keys[keys.currentIndex].id);
    }

    function chooseExistingDir() {
        if (filePicker.available)
            filePicker.openFolder("createPassExisting", i18n.t("db_app.open_existing_pass_pick"));
        else
            existingFolderDialog.open();
    }

    Connections {
        target: filePicker
        enabled: root.visible

        function onPicked(purpose, path) {
            if (purpose === "createPassExisting" && path.length > 0)
                root.existingSelected(path);
        }
        function onFailed(purpose, error) {
            if (purpose === "createPassExisting")
                controller.showMessage(error, true);
        }
    }

    FolderDialog {
        id: existingFolderDialog
        title: i18n.t("db_app.open_existing_pass_pick")
        onAccepted: root.existingSelected(String(selectedFolder))
    }

    Field {
        id: directoryField
        width: parent.width
        label: i18n.t("db_app.dir_label")
        enabled: !root.busy

        onTextChanged: root.refreshSuggestions()
        onCancelled: root.takeKeyboard()
        onListNext: suggestions.next()
        onListPrevious: suggestions.previous()
        onNextRequested: keyList.forceActiveFocus()
        onPreviousRequested: keyList.forceActiveFocus()
        // Enter completes the highlighted folder when there is one; Tab is
        // always available to jump straight to the key list.
        onSubmitted: if (!root.acceptSuggestion()) keyList.forceActiveFocus()
    }

    SuggestionList {
        id: suggestions
        width: parent.width
        labelFor: function(item) { return String(item).split("/").pop(); }
        onPicked: root.acceptSuggestion()
    }

    Text {
        width: parent.width
        text: i18n.t("db_app.gpg_key_label")
        color: keyList.activeFocus ? theme.foreground : theme.muted
        font.pixelSize: Math.round(13 * root.s)
    }

    Text {
        width: parent.width
        visible: root.keys.length === 0
        text: i18n.t("db_app.no_gpg_key_found")
        color: theme.alertWarn
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(13 * root.s)
    }

    FocusScope {
        id: keyList
        width: parent.width
        height: keys.height
        visible: root.keys.length > 0

        SuggestionList {
            id: keys
            width: parent.width
            visibleRows: 5
            model: root.keys
            labelFor: function(item) { return item.id + "  " + item.identity; }
            onPicked: root.submit()
        }

        Keys.onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier) {
                if (event.key === Qt.Key_N) {
                    keys.next();
                    event.accepted = true;
                } else if (event.key === Qt.Key_P) {
                    keys.previous();
                    event.accepted = true;
                }
                return;
            }

            switch (event.key) {
            case Qt.Key_Down:
            case Qt.Key_J:
                keys.next();
                break;
            case Qt.Key_Up:
            case Qt.Key_K:
                keys.previous();
                break;
            case Qt.Key_Tab:
            case Qt.Key_Backtab:
                directoryField.field.forceActiveFocus();
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
                root.submit();
                break;
            case Qt.Key_Escape:
                root.takeKeyboard();
                break;
            default:
                return;
            }
            event.accepted = true;
        }
    }

    AppButton {
        width: parent.width
        enabled: !root.busy
        text: i18n.t("db_app.open_existing_pass")
        onClicked: root.chooseExistingDir()
    }
}
