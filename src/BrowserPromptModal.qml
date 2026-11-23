import QtQuick

// What the browser extension asks of the user, one question at a time:
// linking a browser to the open vault (a name for it), letting a site have
// logins, overwriting a saved password, and — when turning the integration
// on — replacing another program's browser setup. The question comes from
// the browser integration (controller.browser.prompt); the answer goes
// back to it, and an empty prompt hides the sheet.
Modal {
    id: root

    readonly property var integration: controller.browser
    readonly property var prompt: integration.prompt
    readonly property string kind: prompt.kind !== undefined ? prompt.kind : ""
    readonly property bool asksYesNo: kind === "update" || kind === "conflict"

    visible: kind.length > 0
    // Over whatever sheet the page has open: the browser is waiting.
    z: 10
    accentColor: theme.important
    heading: kind === "associate" ? i18n.t("browser.associate_title")
           : kind === "access" ? i18n.t("browser.access_title")
           : kind === "update" ? i18n.t("browser.update_title")
           : i18n.t("browser.conflict_title")
    hint: asksYesNo ? i18n.t("common.yes_no_hint")
        : kind === "access" ? i18n.t("browser.access_hint")
        : i18n.t("browser.associate_hint")
    cardWidth: Math.round(520 * s)

    capturesKeys: asksYesNo

    onShown: {
        if (root.kind === "associate") {
            nameField.text = "";
            nameField.field.forceActiveFocus();
        } else if (root.kind === "access") {
            rememberBox.checked = true;
            // The boxes are made for the new question after this runs.
            Qt.callLater(root.focusFirstEntry);
        }
    }

    function focusFirstEntry() {
        if (root.kind !== "access")
            return;
        if (entryBoxes.count > 0)
            entryBoxes.itemAt(0).forceActiveFocus();
        else
            rememberBox.forceActiveFocus();
    }

    // A second question right after the first keeps the sheet up: the
    // fields reset for the new one all the same.
    onPromptChanged: if (visible) shown()

    onDismissed: refuse()

    function refuse() {
        if (root.kind === "associate")
            root.integration.answerAssociate("");
        else if (root.kind === "access")
            root.integration.answerAccess([], rememberBox.checked);
        else if (root.kind === "update")
            root.integration.answerUpdate(false);
        else if (root.kind === "conflict")
            root.integration.answerConflict(false);
    }

    function allowChecked() {
        const allowed = [];
        for (let i = 0; i < entryBoxes.count; ++i) {
            const box = entryBoxes.itemAt(i);
            if (box.checked)
                allowed.push(box.path);
        }
        root.integration.answerAccess(allowed, rememberBox.checked);
    }

    function submitName() {
        if (nameField.text.trim().length > 0)
            root.integration.answerAssociate(nameField.text);
    }

    readonly property var accessOrder: {
        const order = [];
        for (let i = 0; i < entryBoxes.count; ++i)
            order.push(entryBoxes.itemAt(i));
        order.push(rememberBox);
        return order;
    }

    onKeyPressed: function(event) {
        switch (event.key) {
        case Qt.Key_Y:
            if (root.kind === "update")
                root.integration.answerUpdate(true);
            else
                root.integration.answerConflict(true);
            break;
        case Qt.Key_N:
        case Qt.Key_Escape:
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.refuse();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    // ------------------------------------------------------------ associate

    Text {
        width: parent.width
        visible: root.kind === "associate"
        text: i18n.t("browser.associate_text", { "vault": root.prompt.vault || "" })
        color: theme.base
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(13 * root.s)
    }

    Field {
        id: nameField
        objectName: "associateName"
        width: parent.width
        visible: root.kind === "associate"
        label: i18n.t("browser.associate_name_label")
        placeholderText: "firefox-notebook"

        onSubmitted: root.submitName()
        onCancelled: root.refuse()
    }

    Text {
        width: parent.width
        visible: root.kind === "associate" && root.integration.associationExists(nameField.text)
        text: i18n.t("browser.associate_exists")
        color: theme.alertWarn
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(11 * root.s)
    }

    // ------------------------------------------------------------ access

    Text {
        width: parent.width
        visible: root.kind === "access"
        text: i18n.t("browser.access_text", { "site": root.prompt.site || "" })
        color: theme.base
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(13 * root.s)
    }

    Repeater {
        id: entryBoxes
        model: root.kind === "access" ? root.prompt.entries : []

        CheckOption {
            id: entryBox
            required property var modelData
            required property int index
            readonly property string path: modelData.path
            // Made anew for each question, ticked: the usual answer is
            // "yes, these".
            checked: true

            text: modelData.login && modelData.login.length > 0
                ? modelData.path + "  (" + modelData.login + ")"
                : modelData.path

            onSubmitted: root.allowChecked()
            onCancelled: root.refuse()
            onNextRequested: root.moveFocus(root.accessOrder, entryBox, 1)
            onPreviousRequested: root.moveFocus(root.accessOrder, entryBox, -1)
        }
    }

    CheckOption {
        id: rememberBox
        objectName: "accessRemember"
        visible: root.kind === "access"
        text: i18n.t("browser.access_remember")

        onSubmitted: root.allowChecked()
        onCancelled: root.refuse()
        onNextRequested: root.moveFocus(root.accessOrder, rememberBox, 1)
        onPreviousRequested: root.moveFocus(root.accessOrder, rememberBox, -1)
    }

    // ------------------------------------------------------------ yes/no

    Text {
        width: parent.width
        visible: root.asksYesNo
        text: root.kind === "update"
            ? i18n.t("browser.update_text", { "name": root.prompt.name || "",
                                              "login": root.prompt.login || "",
                                              "site": root.prompt.site || "" })
            : i18n.t("browser.conflict_text",
                     { "browsers": root.prompt.browsers ? root.prompt.browsers.join(", ") : "" })
        color: theme.base
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pixelSize: Math.round(14 * root.s)
    }
}
