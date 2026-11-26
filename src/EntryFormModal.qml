import QtQuick
import QtQuick.Controls

// Add or edit an entry. A pass entry only exposes Group, Title and Password —
// the pass format is far less structured than KeePassXC's, and the fields it
// does not model are preserved untouched through `extra`.
Modal {
    id: root

    property bool sshSupported: false
    property bool noteSupported: false
    property bool cardSupported: false
    property bool identitySupported: false
    readonly property var kindKeys: {
        const list = ["login"];
        if (sshSupported)
            list.push("ssh");
        if (noteSupported)
            list.push("note");
        if (cardSupported)
            list.push("card");
        if (identitySupported)
            list.push("identity");
        return list;
    }
    readonly property var kindLabels: {
        const list = [i18n.t("common.password_label")];
        if (sshSupported)
            list.push("SSH");
        if (noteSupported)
            list.push(i18n.t("common.note_label"));
        if (cardSupported)
            list.push(i18n.t("common.card_label"));
        if (identitySupported)
            list.push(i18n.t("common.identity_label"));
        return list;
    }
    readonly property string selectedKind: isEdit ? (fields.type || "login") : (kindKeys[kinds.currentIndex] || "login")
    readonly property bool sshEntry: selectedKind === "ssh"
    readonly property bool noteEntry: selectedKind === "note"
    readonly property bool cardEntry: selectedKind === "card"
    readonly property bool identityEntry: selectedKind === "identity"
    readonly property bool sshAttachment: isEdit && !!fields.sshAttachment
    readonly property bool inlineSshKey: sshEntry && !sshAttachment
    readonly property bool hideLoginFields: inlineSshKey || noteEntry || cardEntry || identityEntry
    property bool resumeDraft: false
    property bool isEdit: false
    property bool passBackend: false
    // 1Password needs a vault for every item; the other backends keep an
    // entry with no group at the root.
    property bool groupRequired: false
    property var fields: ({})
    property var selectedCollections: []
    property string lockedOrgId: ""

    signal submitted(var payload)
    signal generateRequested()

    heading: isEdit ? i18n.t("common.edit_entry_title") : i18n.t("common.new_entry_title")
    acceptText: i18n.t("common.save")
    cardWidth: Math.round(560 * s)

    acceptEnabled: !inlineSshKey || privateKey.text.trim().length > 0

    onAcceptRequested: root.submit()

    firstField: root.isEdit
        ? (root.inlineSshKey ? privateKey.field
           : root.noteEntry ? notesArea
           : root.cardEntry ? cardholderNameField.field
           : root.identityEntry ? idFirstNameField.field
           : passwordField.field)
        : groupField.field

    onClosed: {
        privateKey.text = "";
        publicKey.text = "";
        cardNumberField.text = "";
        cardCodeField.text = "";
        idSsnField.text = "";
        idPassportNumberField.text = "";
        idLicenseNumberField.text = "";
    }

    onShown: {
        if (resumeDraft) {
            resumeDraft = false;
            return;
        }
        kinds.currentIndex = 0;
        privateKey.text = inlineSshKey ? (fields.password || "") : "";
        publicKey.text = fields.sshPublicKey || "";
        groupField.text = root.fields.group || "";
        titleField.text = root.fields.title || "";
        usernameField.text = root.fields.username || "";
        passwordField.text = inlineSshKey ? "" : (root.fields.password || "");
        urlField.text = root.fields.url || "";
        extraUrlsField.text = (root.fields.extraUrls || []).join(" ");
        notesArea.text = root.fields.notes || "";
        otpField.text = root.fields.otp || "";
        cardholderNameField.text = root.fields.cardholderName || "";
        cardBrandField.text = root.fields.cardBrand || "";
        cardNumberField.text = root.fields.cardNumber || "";
        cardExpMonthField.text = root.fields.cardExpMonth || "";
        cardExpYearField.text = root.fields.cardExpYear || "";
        cardCodeField.text = root.fields.cardCode || "";
        idTitleField.text = root.fields.idTitle || "";
        idFirstNameField.text = root.fields.idFirstName || "";
        idMiddleNameField.text = root.fields.idMiddleName || "";
        idLastNameField.text = root.fields.idLastName || "";
        idCompanyField.text = root.fields.idCompany || "";
        idEmailField.text = root.fields.idEmail || "";
        idPhoneField.text = root.fields.idPhone || "";
        idUsernameField.text = root.fields.idUsername || "";
        idAddress1Field.text = root.fields.idAddress1 || "";
        idAddress2Field.text = root.fields.idAddress2 || "";
        idAddress3Field.text = root.fields.idAddress3 || "";
        idCityField.text = root.fields.idCity || "";
        idStateField.text = root.fields.idState || "";
        idPostalCodeField.text = root.fields.idPostalCode || "";
        idCountryField.text = root.fields.idCountry || "";
        idSsnField.text = root.fields.idSsn || "";
        idPassportNumberField.text = root.fields.idPassportNumber || "";
        idLicenseNumberField.text = root.fields.idLicenseNumber || "";
        customFieldsModel.clear();
        const cfs = root.fields.customFields || [];
        for (let i = 0; i < cfs.length; ++i) {
            customFieldsModel.append({
                "name": String(cfs[i].name || ""),
                "value": String(cfs[i].value || ""),
                "hidden": cfs[i].type === 1
            });
        }
        root.selectedCollections = (root.fields.collectionIds || []).slice();
        root.lockedOrgId = root.fields.organizationId || "";
        refreshGroups();
    }

    function setPassword(password) {
        passwordField.text = password;
    }

    // With the field empty the groups are still listed, but none is picked
    // up front: Enter then keeps the entry at the root, and the arrows reach
    // the list. Only where a group is mandatory is the first one preselected.
    function refreshGroups() {
        groupSuggestions.model = controller.matchingGroups(groupField.text);
        groupSuggestions.currentIndex = (groupField.text.length === 0 && !root.groupRequired) ? -1 : 0;
    }

    function acceptGroupSuggestion() {
        if (groupSuggestions.model.length === 0 || groupSuggestions.currentIndex < 0)
            return false;

        groupField.text = groupSuggestions.model[groupSuggestions.currentIndex];
        titleField.field.forceActiveFocus();
        return true;
    }

    function submit() {
        if (!root.acceptEnabled)
            return;
        const hideLogin = root.hideLoginFields;
        const payload = {
            "type": root.selectedKind,
            "sshPublicKey": root.sshEntry ? publicKey.text : "",
            "isEdit": root.isEdit,
            "originalPath": root.fields.originalPath || "",
            "group": groupField.text,
            "title": titleField.text,
            "username": hideLogin ? "" : usernameField.text,
            "password": root.inlineSshKey ? privateKey.text : (hideLogin ? "" : passwordField.text),
            "url": hideLogin ? "" : urlField.text,
            "extraUrls": hideLogin ? "" : extraUrlsField.text,
            "notes": notesArea.text,
            "extra": root.fields.extra || [],
            // The controller keeps the original as it was when the field was
            // not touched, and checks and normalises it when it was.
            "otp": hideLogin ? "" : otpField.text,
            "originalOtp": root.fields.otp || "",
            "cardholderName": root.cardEntry ? cardholderNameField.text : "",
            "cardBrand": root.cardEntry ? cardBrandField.text : "",
            "cardNumber": root.cardEntry ? cardNumberField.text : "",
            "cardExpMonth": root.cardEntry ? cardExpMonthField.text : "",
            "cardExpYear": root.cardEntry ? cardExpYearField.text : "",
            "cardCode": root.cardEntry ? cardCodeField.text : "",
            "idTitle": root.identityEntry ? idTitleField.text : "",
            "idFirstName": root.identityEntry ? idFirstNameField.text : "",
            "idMiddleName": root.identityEntry ? idMiddleNameField.text : "",
            "idLastName": root.identityEntry ? idLastNameField.text : "",
            "idCompany": root.identityEntry ? idCompanyField.text : "",
            "idEmail": root.identityEntry ? idEmailField.text : "",
            "idPhone": root.identityEntry ? idPhoneField.text : "",
            "idUsername": root.identityEntry ? idUsernameField.text : "",
            "idAddress1": root.identityEntry ? idAddress1Field.text : "",
            "idAddress2": root.identityEntry ? idAddress2Field.text : "",
            "idAddress3": root.identityEntry ? idAddress3Field.text : "",
            "idCity": root.identityEntry ? idCityField.text : "",
            "idState": root.identityEntry ? idStateField.text : "",
            "idPostalCode": root.identityEntry ? idPostalCodeField.text : "",
            "idCountry": root.identityEntry ? idCountryField.text : "",
            "idSsn": root.identityEntry ? idSsnField.text : "",
            "idPassportNumber": root.identityEntry ? idPassportNumberField.text : "",
            "idLicenseNumber": root.identityEntry ? idLicenseNumberField.text : ""
        };
        if (root.noteSupported) {
            const cfs = [];
            for (let i = 0; i < customFieldsModel.count; ++i) {
                const row = customFieldsModel.get(i);
                cfs.push({ "name": row.name, "value": row.value, "type": row.hidden ? 1 : 0 });
            }
            payload.customFields = cfs;
            payload.collectionIds = root.selectedCollections;
        }
        root.submitted(payload);
    }

    // The fields the pass backend does not show are hidden, which is how
    // moveFocus knows to skip them.
    readonly property var tabOrder: [
        kinds, groupField, titleField, privateKey, publicKey,
        usernameField, passwordField, revealPassword, otpField, urlField, extraUrlsField,
        cardholderNameField, cardBrandField, cardNumberField, cardExpMonthField, cardExpYearField, cardCodeField,
        idTitleField, idFirstNameField, idMiddleNameField, idLastNameField, idCompanyField,
        idEmailField, idPhoneField, idUsernameField, idAddress1Field, idAddress2Field, idAddress3Field,
        idCityField, idStateField, idPostalCodeField, idCountryField,
        idSsnField, idPassportNumberField, idLicenseNumberField,
        notesArea
    ]

    function focusNext(from) { root.moveFocus(root.tabOrder, from, 1); }
    function focusPrevious(from) { root.moveFocus(root.tabOrder, from, -1); }

    Segmented {
        id: kinds
        objectName: "entryKinds"
        visible: !root.isEdit && root.kindKeys.length > 1
        options: root.kindLabels
        onNextRequested: root.focusNext(kinds)
        onPreviousRequested: root.focusPrevious(kinds)
        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
    }

    Field {
        id: groupField
        width: parent.width
        label: i18n.t("common.group_label")
        placeholderText: root.groupRequired ? "" : i18n.t("common.no_group_placeholder")

        onTextChanged: root.refreshGroups()
        onCancelled: root.takeKeyboard()
        onListNext: groupSuggestions.next()
        onListPrevious: groupSuggestions.previous()
        onNextRequested: root.focusNext(groupField)
        onPreviousRequested: root.focusPrevious(groupField)
        onSubmitted: {
            if (root.acceptGroupSuggestion())
                return;
            if (groupField.text.trim().length === 0 && !root.groupRequired)
                titleField.field.forceActiveFocus();
            else
                root.submit();
        }
    }

    SuggestionList {
        id: groupSuggestions
        width: parent.width
        onPicked: root.acceptGroupSuggestion()
    }

    Column {
        id: collectionsSection
        visible: root.noteSupported && controller.bitwardenCollections.length > 0
        width: parent.width
        spacing: Math.round(6 * root.s)

        readonly property string activeOrgId: {
            if (root.lockedOrgId.length > 0)
                return root.lockedOrgId;
            if (root.selectedCollections.length === 0)
                return "";
            const cols = controller.bitwardenCollections;
            for (let i = 0; i < cols.length; ++i) {
                if (cols[i].id === root.selectedCollections[0])
                    return cols[i].organizationId || "";
            }
            return "";
        }

        function toggleCollection(id, orgId) {
            const list = root.selectedCollections.slice();
            const idx = list.indexOf(id);
            if (idx >= 0) {
                if (root.lockedOrgId.length > 0 && list.length <= 1)
                    return;
                list.splice(idx, 1);
            } else {
                if (activeOrgId.length > 0 && orgId !== activeOrgId)
                    return;
                list.push(id);
            }
            root.selectedCollections = list;
        }

        Text {
            text: i18n.t("common.collections_label")
            color: theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        Flow {
            width: parent.width
            spacing: Math.round(6 * root.s)

            Repeater {
                model: controller.bitwardenCollections

                Rectangle {
                    required property var modelData
                    readonly property bool picked: root.selectedCollections.indexOf(modelData.id) >= 0
                    readonly property bool allowed: collectionsSection.activeOrgId.length === 0
                        || modelData.organizationId === collectionsSection.activeOrgId
                    visible: allowed || picked
                    width: Math.min(colChipLabel.implicitWidth + Math.round(20 * root.s), collectionsSection.width)
                    height: Math.round(28 * root.s)
                    radius: Math.min(4, theme.radius)
                    color: picked ? theme.selection : (chipHover.containsMouse ? theme.hover : theme.background)
                    border.width: 1
                    border.color: picked ? theme.accent : theme.border

                    Text {
                        id: colChipLabel
                        anchors.centerIn: parent
                        width: parent.width - Math.round(14 * root.s)
                        text: modelData.name
                        color: parent.picked ? theme.selectionText : theme.foreground
                        font.pixelSize: Math.round(12 * root.s)
                        font.weight: parent.picked ? Font.DemiBold : Font.Normal
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                    }

                    MouseArea {
                        id: chipHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: collectionsSection.toggleCollection(modelData.id, modelData.organizationId || "")
                    }
                }
            }
        }
    }

    Field {
        id: titleField
        width: parent.width
        label: i18n.t("common.title_label")

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(titleField)
        onPreviousRequested: root.focusPrevious(titleField)
    }

    Field {
        id: usernameField
        width: parent.width
        visible: !root.passBackend && !root.hideLoginFields
        label: i18n.t("common.username_label")

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(usernameField)
        onPreviousRequested: root.focusPrevious(usernameField)
    }

    Field {
        id: passwordField
        visible: !root.hideLoginFields
        width: parent.width
        label: i18n.t("common.password_label")
        echoMode: revealPassword.checked ? TextInput.Normal : TextInput.Password
        monospace: true

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(passwordField)
        onPreviousRequested: root.focusPrevious(passwordField)
        onGenerateRequested: root.generateRequested()
    }

    CheckOption {
        id: revealPassword
        visible: passwordField.visible
        text: i18n.t("ui.show_password")

        onSubmitted: root.submit()
        onCancelled: root.takeKeyboard()
        onNextRequested: root.focusNext(revealPassword)
        onPreviousRequested: root.focusPrevious(revealPassword)
    }

    // Every backend keeps one, pass included (pass-otp's line). The secret
    // a site shows when setting it up, or the otpauth:// URI behind its QR
    // code, both go in as they are.
    Field {
        id: otpField
        visible: !root.hideLoginFields
        objectName: "otpField"
        width: parent.width
        label: i18n.t("common.otp_label")
        monospace: true

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(otpField)
        onPreviousRequested: root.focusPrevious(otpField)
    }

    Field {
        id: urlField
        width: parent.width
        visible: !root.passBackend && !root.hideLoginFields
        label: i18n.t("common.url_label")

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(urlField)
        onPreviousRequested: root.focusPrevious(urlField)
    }

    // Other addresses the same login is for, as KeePassXC's additional
    // URLs: the browser offers the entry on any of them.
    Field {
        id: extraUrlsField
        objectName: "extraUrlsField"
        width: parent.width
        visible: !root.passBackend && !root.hideLoginFields
        label: i18n.t("common.extra_urls_label")
        placeholderText: "https://exemplo.com https://login.exemplo.com"

        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(extraUrlsField)
        onPreviousRequested: root.focusPrevious(extraUrlsField)
    }

    component KeyField: Column {
        id: keyField
        required property string label
        property alias text: editor.text
        property alias field: editor
        property alias editorName: editor.objectName
        property alias placeholder: editor.placeholderText
        width: parent.width
        spacing: Math.round(6 * root.s)

        Text {
            text: keyField.label
            color: editor.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }
        ScrollView {
            width: parent.width
            height: Math.round(90 * root.s)
            contentWidth: availableWidth
            background: Rectangle {
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.color: editor.activeFocus ? theme.accent : theme.border
            }
            TextArea {
                id: editor
                background: null
                padding: Math.round(10 * root.s)
                font.family: systemTheme.monoFamily
                wrapMode: TextArea.WrapAnywhere
                color: theme.foreground
                placeholderTextColor: theme.muted
                selectionColor: theme.selection
                selectedTextColor: theme.selectionText
                Keys.onEscapePressed: root.takeKeyboard()
                Keys.onTabPressed: root.focusNext(keyField)
                Keys.onBacktabPressed: root.focusPrevious(keyField)
            }
        }
    }

    KeyField {
        id: privateKey
        editorName: "sshPrivateKey"
        label: i18n.t("ssh.private_label")
        placeholder: i18n.t("ssh.private_key")
        visible: root.inlineSshKey
    }

    KeyField {
        id: publicKey
        editorName: "sshPublicKey"
        label: i18n.t("ssh.public_label")
        placeholder: i18n.t("ssh.public_hint")
        visible: root.sshEntry
    }

    // --- Card fields (Bitwarden type 3) ---
    Field {
        id: cardholderNameField
        width: parent.width
        visible: root.cardEntry
        label: i18n.t("card.cardholder_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(cardholderNameField)
        onPreviousRequested: root.focusPrevious(cardholderNameField)
    }
    Field {
        id: cardBrandField
        width: parent.width
        visible: root.cardEntry
        label: i18n.t("card.brand_label")
        placeholderText: "Visa, Mastercard, Elo…"
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(cardBrandField)
        onPreviousRequested: root.focusPrevious(cardBrandField)
    }
    Field {
        id: cardNumberField
        width: parent.width
        visible: root.cardEntry
        label: i18n.t("card.number_label")
        monospace: true
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(cardNumberField)
        onPreviousRequested: root.focusPrevious(cardNumberField)
    }
    Field {
        id: cardExpMonthField
        width: parent.width
        visible: root.cardEntry
        label: i18n.t("card.exp_month_label")
        placeholderText: "01 - 12"
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(cardExpMonthField)
        onPreviousRequested: root.focusPrevious(cardExpMonthField)
    }
    Field {
        id: cardExpYearField
        width: parent.width
        visible: root.cardEntry
        label: i18n.t("card.exp_year_label")
        placeholderText: "2028"
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(cardExpYearField)
        onPreviousRequested: root.focusPrevious(cardExpYearField)
    }
    Field {
        id: cardCodeField
        width: parent.width
        visible: root.cardEntry
        label: i18n.t("card.code_label")
        monospace: true
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(cardCodeField)
        onPreviousRequested: root.focusPrevious(cardCodeField)
    }

    // --- Identity fields (Bitwarden type 4) ---
    Field {
        id: idTitleField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.title_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idTitleField)
        onPreviousRequested: root.focusPrevious(idTitleField)
    }
    Field {
        id: idFirstNameField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.first_name_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idFirstNameField)
        onPreviousRequested: root.focusPrevious(idFirstNameField)
    }
    Field {
        id: idMiddleNameField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.middle_name_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idMiddleNameField)
        onPreviousRequested: root.focusPrevious(idMiddleNameField)
    }
    Field {
        id: idLastNameField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.last_name_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idLastNameField)
        onPreviousRequested: root.focusPrevious(idLastNameField)
    }
    Field {
        id: idCompanyField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.company_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idCompanyField)
        onPreviousRequested: root.focusPrevious(idCompanyField)
    }
    Field {
        id: idEmailField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.email_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idEmailField)
        onPreviousRequested: root.focusPrevious(idEmailField)
    }
    Field {
        id: idPhoneField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.phone_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idPhoneField)
        onPreviousRequested: root.focusPrevious(idPhoneField)
    }
    Field {
        id: idUsernameField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("common.username_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idUsernameField)
        onPreviousRequested: root.focusPrevious(idUsernameField)
    }
    Field {
        id: idAddress1Field
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.address1_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idAddress1Field)
        onPreviousRequested: root.focusPrevious(idAddress1Field)
    }
    Field {
        id: idAddress2Field
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.address2_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idAddress2Field)
        onPreviousRequested: root.focusPrevious(idAddress2Field)
    }
    Field {
        id: idAddress3Field
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.address3_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idAddress3Field)
        onPreviousRequested: root.focusPrevious(idAddress3Field)
    }
    Field {
        id: idCityField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.city_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idCityField)
        onPreviousRequested: root.focusPrevious(idCityField)
    }
    Field {
        id: idStateField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.state_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idStateField)
        onPreviousRequested: root.focusPrevious(idStateField)
    }
    Field {
        id: idPostalCodeField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.postal_code_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idPostalCodeField)
        onPreviousRequested: root.focusPrevious(idPostalCodeField)
    }
    Field {
        id: idCountryField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.country_label")
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idCountryField)
        onPreviousRequested: root.focusPrevious(idCountryField)
    }
    Field {
        id: idSsnField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.ssn_label")
        monospace: true
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idSsnField)
        onPreviousRequested: root.focusPrevious(idSsnField)
    }
    Field {
        id: idPassportNumberField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.passport_label")
        monospace: true
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idPassportNumberField)
        onPreviousRequested: root.focusPrevious(idPassportNumberField)
    }
    Field {
        id: idLicenseNumberField
        width: parent.width
        visible: root.identityEntry
        label: i18n.t("identity.license_label")
        monospace: true
        onCancelled: root.takeKeyboard()
        onSubmitted: root.submit()
        onNextRequested: root.focusNext(idLicenseNumberField)
        onPreviousRequested: root.focusPrevious(idLicenseNumberField)
    }

    Column {
        width: parent.width
        visible: !root.passBackend
        spacing: Math.round(6 * root.s)

        Text {
            text: i18n.t("common.notes_label")
            color: notesArea.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        ScrollView {
            objectName: "formNotesBox"
            contentWidth: availableWidth
            width: parent.width
            height: Math.round((root.noteEntry ? 180 : 90) * root.s)
            Component.onCompleted: contentItem.boundsBehavior = Flickable.StopAtBounds

            background: Rectangle {
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.width: 1
                border.color: notesArea.activeFocus ? theme.accent : theme.border
            }

            TextArea {
                id: notesArea
                color: theme.foreground
                selectionColor: theme.selection
                selectedTextColor: theme.selectionText
                wrapMode: TextArea.Wrap
                font.pixelSize: Math.round(14 * root.s)
                leftPadding: Math.round(12 * root.s)
                rightPadding: Math.round(12 * root.s)
                topPadding: Math.round(9 * root.s)
                bottomPadding: Math.round(9 * root.s)
                background: null

                // Enter inserts a line break here, as it did in the TUI;
                // Ctrl+Enter is what saves from inside the notes.
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) {
                        root.takeKeyboard();
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Tab) {
                        root.focusNext(notesArea);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Backtab) {
                        root.focusPrevious(notesArea);
                        event.accepted = true;
                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                               && (event.modifiers & Qt.ControlModifier)) {
                        root.submit();
                        event.accepted = true;
                    }
                }
            }
        }
    }

    ListModel { id: customFieldsModel }

    Column {
        width: parent.width
        visible: root.noteSupported
        spacing: Math.round(8 * root.s)

        Row {
            width: parent.width
            spacing: Math.round(8 * root.s)

            Text {
                text: i18n.t("common.custom_fields_label")
                color: theme.muted
                font.pixelSize: Math.round(13 * root.s)
                anchors.verticalCenter: parent.verticalCenter
            }

            AppButton {
                flat: true
                text: i18n.t("common.add_custom_field")
                onClicked: customFieldsModel.append({ "name": "", "value": "", "hidden": false })
            }
        }

        Repeater {
            model: customFieldsModel

            Column {
                required property int index
                required property string name
                required property string value
                required property bool hidden
                width: parent.width
                spacing: Math.round(6 * root.s)

                Row {
                    width: parent.width
                    spacing: Math.round(6 * root.s)

                    Field {
                        width: Math.round((parent.width - removeBtn.width - parent.spacing * 2) * 0.4)
                        placeholderText: i18n.t("common.field_name")
                        text: name
                        onTextChanged: if (text !== name) customFieldsModel.setProperty(index, "name", text)
                        onCancelled: root.takeKeyboard()
                        onSubmitted: root.submit()
                    }

                    Field {
                        width: Math.round((parent.width - removeBtn.width - parent.spacing * 2) * 0.6)
                        placeholderText: i18n.t("common.field_value")
                        text: value
                        echoMode: hidden ? TextInput.Password : TextInput.Normal
                        monospace: hidden
                        onTextChanged: if (text !== value) customFieldsModel.setProperty(index, "value", text)
                        onCancelled: root.takeKeyboard()
                        onSubmitted: root.submit()
                    }

                    AppButton {
                        id: removeBtn
                        flat: true
                        text: "×"
                        onClicked: customFieldsModel.remove(index)
                    }
                }

                CheckOption {
                    text: i18n.t("common.field_hidden")
                    checked: hidden
                    onCheckedChanged: if (checked !== hidden) customFieldsModel.setProperty(index, "hidden", checked)
                }
            }
        }
    }
}
