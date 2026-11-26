import QtQuick
import QtQuick.Controls

// Read-only view of an entry, opened with Tab. Selecting text copies it, the
// way dragging over a field did in the TUI; Enter copies the whole field
// under the cursor, and Tab cycles between the fields.
//
// A pass entry only shows the Title — the format has no URL or Notes of its
// own. An entry with a one-time password, in any backend, also shows its
// current code and how long it has left; the secret stays in the controller.
Modal {
    id: root

    property bool passBackend: false
    property var details: ({})
    readonly property bool isSsh: !!root.details.isSsh
    readonly property bool isNote: !!root.details.isNote
    readonly property bool isCard: !!root.details.isCard
    readonly property bool isIdentity: !!root.details.isIdentity
    readonly property bool isNonLogin: root.isSsh || root.isNote || root.isCard || root.isIdentity
    readonly property bool hasOtp: !root.isNonLogin && !!root.details.hasOtp
    readonly property bool hasExtraUrls: !root.isNonLogin && (root.details.extraUrls || []).length > 0
    readonly property bool showNotes: !root.passBackend && (!root.isNonLogin || root.isNote || (root.details.notes || "").length > 0)
    readonly property var structuredFields: {
        const list = [];
        function add(label, value, secret, mono) {
            if (value && String(value).length > 0)
                list.push({ "label": label, "value": String(value), "secret": !!secret, "mono": !!mono });
        }
        if (root.isSsh) {
            add(i18n.t("common.ssh_public_key_label"), root.details.sshPublicKey, false, true);
        } else if (root.isCard) {
            add(i18n.t("card.cardholder_label"), root.details.cardholderName, false, false);
            add(i18n.t("card.brand_label"), root.details.cardBrand, false, false);
            add(i18n.t("card.number_label"), root.details.cardNumber, true, true);
            const expM = root.details.cardExpMonth || "";
            const expY = root.details.cardExpYear || "";
            if (expM || expY)
                add(i18n.t("card.expiration_label"), expM && expY ? (expM + "/" + expY) : (expM || expY), false, true);
            add(i18n.t("card.code_label"), root.details.cardCode, true, true);
        } else if (root.isIdentity) {
            add(i18n.t("identity.title_label"), root.details.idTitle, false, false);
            add(i18n.t("identity.first_name_label"), root.details.idFirstName, false, false);
            add(i18n.t("identity.middle_name_label"), root.details.idMiddleName, false, false);
            add(i18n.t("identity.last_name_label"), root.details.idLastName, false, false);
            add(i18n.t("identity.company_label"), root.details.idCompany, false, false);
            add(i18n.t("identity.email_label"), root.details.idEmail, false, false);
            add(i18n.t("identity.phone_label"), root.details.idPhone, false, false);
            add(i18n.t("common.username_label"), root.details.idUsername, false, false);
            add(i18n.t("identity.address1_label"), root.details.idAddress1, false, false);
            add(i18n.t("identity.address2_label"), root.details.idAddress2, false, false);
            add(i18n.t("identity.address3_label"), root.details.idAddress3, false, false);
            add(i18n.t("identity.city_label"), root.details.idCity, false, false);
            add(i18n.t("identity.state_label"), root.details.idState, false, false);
            add(i18n.t("identity.postal_code_label"), root.details.idPostalCode, false, false);
            add(i18n.t("identity.country_label"), root.details.idCountry, false, false);
            add(i18n.t("identity.ssn_label"), root.details.idSsn, true, true);
            add(i18n.t("identity.passport_label"), root.details.idPassportNumber, true, true);
            add(i18n.t("identity.license_label"), root.details.idLicenseNumber, true, true);
        }
        return list;
    }

    function structuredView(index) {
        const field = structuredViews.itemAt(index);
        return field ? field.view : null;
    }

    // The Tab chain through the additional URLs, which come and go with the
    // entry shown.
    function extraUrlView(index) {
        const field = extraUrlViews.itemAt(index);
        return field ? field.view : null;
    }
    function fieldAfterUrls() {
        return root.hasOtp ? otpView.view : notesView.view;
    }
    function lastUrlView() {
        return extraUrlViews.count > 0 ? root.extraUrlView(extraUrlViews.count - 1) : urlView.view;
    }
    property var otp: ({})

    // Read from `details` itself: in onDetailsChanged the hasOtp binding may
    // not have caught up yet.
    function refreshOtp() {
        root.otp = root.details.hasOtp ? controller.otpNow() : ({});
    }

    onDetailsChanged: refreshOtp()

    // Every second rather than once per period: the countdown is part of
    // what the code is worth, and a period can start at any second.
    Timer {
        interval: 1000
        repeat: true
        running: root.visible && root.hasOtp
        onTriggered: root.refreshOtp()
    }

    heading: root.details.title || i18n.t("common.entry_info_title")
    subtitle: root.isSsh ? "SSH"
            : root.isNote ? i18n.t("common.note_label")
            : root.isCard ? i18n.t("common.card_label")
            : root.isIdentity ? i18n.t("common.identity_label")
            : i18n.t("common.entry_info_title")
    acceptText: i18n.t("ui.context_edit")
    cancelText: i18n.t("common.close")
    cardWidth: Math.round(600 * s)

    onKeyPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Q) {
            root.dismissed();
            event.accepted = true;
        }
    }

    firstField: titleView.view

    onShown: {
        refreshOtp();
    }

    component InfoField: Column {
        id: fieldColumn

        property string label: ""
        property string value: ""
        property int rows: 1
        property Item nextField: null
        property Item previousField: null
        property alias view: text
        property alias scrollBox: valueBox
        // What Enter does instead of copying the text as it stands: the
        // one-time code goes through the clipboard clearing of a password.
        property var copyAction: null
        // What `o` does on a field holding an address: open it.
        property var openAction: null
        property bool selectable: true
        // A secret (the one-time code): monospace and larger.
        property bool monospace: false

        width: parent.width
        spacing: Math.round(6 * root.s)

        Text {
            text: fieldColumn.label
            color: text.activeFocus ? theme.foreground : theme.muted
            font.pixelSize: Math.round(13 * root.s)
        }

        // The field Tab is on is boxed in the accent, so Enter's target is
        // plain to see.
        ScrollView {
            id: valueBox
            contentWidth: availableWidth
            contentHeight: text.contentHeight
            clip: true
            width: parent.width
            height: Math.round((fieldColumn.monospace ? 26 : 22) * root.s) * fieldColumn.rows
                    + topPadding + bottomPadding
            padding: Math.round(8 * root.s)
            leftPadding: Math.round(12 * root.s)
            rightPadding: Math.round(12 * root.s)

            Component.onCompleted: contentItem.boundsBehavior = Flickable.StopAtBounds

            background: Rectangle {
                color: theme.background
                radius: Math.min(4, theme.radius)
                border.width: 1
                border.color: text.activeFocus ? theme.accent : theme.border
            }

            TextEdit {
                id: text
                width: valueBox.availableWidth
                text: fieldColumn.value
                color: theme.foreground
                readOnly: true
                selectByMouse: fieldColumn.selectable
                selectionColor: theme.selection
                selectedTextColor: theme.selectionText
                wrapMode: TextEdit.Wrap
                font.pixelSize: Math.round((fieldColumn.monospace ? 18 : 14) * root.s)
                font.family: fieldColumn.monospace ? systemTheme.monoFamily : Qt.application.font.family
                font.letterSpacing: fieldColumn.monospace ? 2 : 0

                // Copying once the selection settles reproduces the TUI's
                // select-to-copy without firing on every pixel of the drag.
                onSelectedTextChanged: copyTimer.restart()

                Timer {
                    id: copyTimer
                    interval: 250
                    onTriggered: controller.copyText(text.selectedText)
                }

                Keys.onPressed: function(event) {
                    switch (event.key) {
                    case Qt.Key_Escape:
                        root.takeKeyboard();
                        break;
                    case Qt.Key_Q:
                        root.dismissed();
                        break;
                    case Qt.Key_O:
                        if (!fieldColumn.openAction)
                            return;
                        fieldColumn.openAction();
                        break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        if (fieldColumn.copyAction)
                            fieldColumn.copyAction();
                        else
                            controller.copyText(fieldColumn.value);
                        break;
                    case Qt.Key_Tab:
                        if (fieldColumn.nextField)
                            fieldColumn.nextField.forceActiveFocus();
                        break;
                    case Qt.Key_Backtab:
                        if (fieldColumn.previousField)
                            fieldColumn.previousField.forceActiveFocus();
                        break;
                    default:
                        return;
                    }
                    event.accepted = true;
                }
            }
        }
    }

    InfoField {
        id: titleView
        label: i18n.t("common.title_word")
        value: root.details.title || ""
        nextField: root.passBackend ? (root.hasOtp ? otpView.view : titleView.view)
                 : root.structuredFields.length > 0 ? root.structuredView(0)
                 : root.isNonLogin ? (root.showNotes ? notesView.view : (customFieldViews.count > 0 ? customFieldViews.itemAt(0).view : titleView.view))
                 : usernameView.view
        previousField: root.passBackend ? (root.hasOtp ? otpView.view : titleView.view)
                     : (customFieldViews.count > 0 ? customFieldViews.itemAt(customFieldViews.count - 1).view
                        : (root.showNotes ? notesView.view
                           : (root.structuredFields.length > 0 ? root.structuredView(structuredViews.count - 1) : titleView.view)))
    }

    Repeater {
        id: structuredViews
        model: root.structuredFields

        InfoField {
            required property var modelData
            required property int index

            label: modelData.label
            value: modelData.value
            monospace: modelData.mono
            copyAction: modelData.secret ? function() { controller.copySecret(modelData.value); } : null
            nextField: index + 1 < structuredViews.count ? root.structuredView(index + 1)
                     : (root.showNotes ? notesView.view
                        : (customFieldViews.count > 0 ? customFieldViews.itemAt(0).view : titleView.view))
            previousField: index > 0 ? root.structuredView(index - 1) : titleView.view
        }
    }

    InfoField {
        id: usernameView
        objectName: "usernameField"
        visible: !root.passBackend && !root.isNonLogin
        label: i18n.t("ui.info_login")
        value: root.details.username || ""
        nextField: urlView.view
        previousField: titleView.view
    }

    InfoField {
        id: urlView
        visible: !root.passBackend && !root.isNonLogin
        label: "URL"
        value: root.details.url || ""
        openAction: root.details.url ? function() { controller.openAddress(root.details.url); } : null
        nextField: root.hasExtraUrls ? root.extraUrlView(0) : root.fieldAfterUrls()
        previousField: usernameView.view
    }

    // One field per additional URL, each copied with Enter and opened with o
    // like the main one.
    Repeater {
        id: extraUrlViews
        model: (root.passBackend || root.isNonLogin) ? [] : (root.details.extraUrls || [])

        InfoField {
            required property string modelData
            required property int index

            objectName: "extraUrlField" + index
            label: i18n.t("ui.info_extra_url", { "n": index + 1 })
            value: modelData
            openAction: function() { controller.openAddress(modelData); }
            nextField: index + 1 < extraUrlViews.count ? root.extraUrlView(index + 1) : root.fieldAfterUrls()
            previousField: index > 0 ? root.extraUrlView(index - 1) : urlView.view
        }
    }

    Column {
        width: parent.width
        visible: root.hasOtp
        spacing: Math.round(4 * root.s)

        InfoField {
            id: otpView
            objectName: "otpField"
            label: i18n.t("ui.info_otp") + "  ·  "
                   + i18n.t("ui.info_otp_seconds", { "seconds": root.otp.secondsLeft || 0 })
            value: root.otp.code || ""
            selectable: false
            monospace: true
            copyAction: controller.copyShownOtp
            nextField: root.passBackend ? titleView.view : notesView.view
            previousField: root.passBackend ? titleView.view : root.lastUrlView()
        }

        // How much of the period is left, emptying towards the next code.
        Rectangle {
            width: parent.width
            height: Math.max(3, Math.round(3 * root.s))
            radius: height / 2
            color: theme.border

            Rectangle {
                height: parent.height
                radius: parent.radius
                width: root.otp.period ? parent.width * root.otp.secondsLeft / root.otp.period : 0
                // The last few seconds in the warning colour: a code copied
                // then may be stale by the time it is pasted.
                color: (root.otp.secondsLeft || 0) <= 5 ? theme.alertWarn : theme.accent
            }
        }
    }

    InfoField {
        id: notesView
        objectName: "notesField"
        visible: root.showNotes
        label: i18n.t("common.notes_label")
        value: root.details.notes || ""
        rows: root.isNote ? 10 : 5
        nextField: customFieldViews.count > 0 ? customFieldViews.itemAt(0).view : titleView.view
        previousField: root.structuredFields.length > 0 ? root.structuredView(structuredViews.count - 1)
                     : root.isNonLogin ? titleView.view
                     : (root.hasOtp ? otpView.view : root.lastUrlView())
    }

    InfoField {
        id: collectionsInfoView
        visible: !root.passBackend && (root.details.collections || "").length > 0
        label: i18n.t("common.collections_label")
        value: root.details.collections || ""
    }

    InfoField {
        id: groupInfoView
        visible: !root.passBackend && (root.details.group || "").length > 0
        label: i18n.t("common.group_label")
        value: root.details.group || ""
    }

    Repeater {
        id: customFieldViews
        model: root.passBackend ? [] : (root.details.customFields || [])

        InfoField {
            required property var modelData
            required property int index

            label: modelData.name || i18n.t("common.custom_fields_label")
            value: modelData.restricted ? i18n.t("common.field_restricted") : (modelData.value || "")
            selectable: !modelData.restricted
            monospace: modelData.type === 1 && !modelData.restricted
            copyAction: modelData.restricted ? function() {}
                      : (modelData.type === 1 ? function() { controller.copySecret(modelData.value || ""); } : null)
            nextField: index + 1 < customFieldViews.count ? customFieldViews.itemAt(index + 1).view : titleView.view
            previousField: index > 0 ? customFieldViews.itemAt(index - 1).view : notesView.view
        }
    }
}
