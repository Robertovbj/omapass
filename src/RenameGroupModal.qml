import QtQuick

// Renames the last segment of a group, keeping its parent and everything
// below it in place.
Modal {
    id: root

    property string currentName: ""

    signal submitted(string name)

    heading: i18n.t("ui.rename_group_title")
    acceptText: i18n.t("common.rename")
    cardWidth: Math.round(440 * s)

    onAcceptRequested: root.submitted(nameField.text)

    capturesKeys: false

    onShown: {
        nameField.text = root.currentName;
        nameField.field.forceActiveFocus();
        nameField.field.selectAll();
    }

    Field {
        id: nameField
        width: parent.width
        label: i18n.t("common.title_label")

        onSubmitted: root.submitted(text)
        onCancelled: root.dismissed()
    }
}
