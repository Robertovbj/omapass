import QtQuick
import QtQuick.Controls
import QtQuick.Window

// Standalone dialog used when OpenSSH invokes omapass as SSH_ASKPASS.
ApplicationWindow {
    id: window

    readonly property real s: systemTheme.textScale
    readonly property bool isConfirm: askpass.mode === "confirm"
    readonly property bool isInfo: askpass.mode === "info"
    readonly property bool isPassphrase: askpass.mode === "passphrase"
    property bool decided: false

    width: Math.round(460 * s)
    minimumWidth: Math.round(360 * s)
    height: Math.max(Math.round(160 * s), content.implicitHeight + Math.round(40 * s))
    minimumHeight: height
    maximumHeight: height
    visible: true
    flags: Qt.Dialog | Qt.WindowStaysOnTopHint
    color: theme.windowBackground
    title: isConfirm ? i18n.t("ssh.askpass_confirm_title")
         : isInfo ? i18n.t("ssh.askpass_info_title")
         : i18n.t("ssh.askpass_passphrase_title")

    function submit() {
        if (decided)
            return;
        decided = true;
        askpass.accept(isPassphrase ? secretField.text : "");
    }

    function cancel() {
        if (decided)
            return;
        decided = true;
        if (isInfo)
            askpass.accept("");
        else
            askpass.reject();
    }

    onClosing: function(event) {
        if (!decided) {
            decided = true;
            if (isInfo)
                askpass.accept("");
            else
                askpass.reject();
        }
    }

    Item {
        id: root
        anchors.fill: parent
        focus: !window.isPassphrase

        Keys.onPressed: function(event) {
            switch (event.key) {
            case Qt.Key_Return:
            case Qt.Key_Enter:
                window.submit();
                event.accepted = true;
                break;
            case Qt.Key_Y:
                if (window.isConfirm) {
                    window.submit();
                    event.accepted = true;
                }
                break;
            case Qt.Key_N:
                if (window.isConfirm) {
                    window.cancel();
                    event.accepted = true;
                }
                break;
            case Qt.Key_Escape:
                window.cancel();
                event.accepted = true;
                break;
            default:
                break;
            }
        }

        Column {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Math.round(20 * window.s)
            spacing: Math.round(14 * window.s)

            Text {
                width: parent.width
                text: window.title
                color: theme.foreground
                font.pixelSize: Math.round(16 * window.s)
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: askpass.prompt.length > 0
                    ? askpass.prompt
                    : (window.isConfirm ? i18n.t("ssh.askpass_confirm_fallback")
                                        : i18n.t("ssh.askpass_passphrase_fallback"))
                color: theme.foreground
                font.pixelSize: Math.round(14 * window.s)
                wrapMode: Text.WordWrap
            }

            Field {
                id: secretField
                visible: window.isPassphrase
                width: parent.width
                echoMode: TextInput.Password
                monospace: true
                placeholderText: i18n.t("ssh.askpass_passphrase_placeholder")
                onSubmitted: window.submit()
                onCancelled: window.cancel()
            }

            Row {
                anchors.right: parent.right
                spacing: Math.round(8 * window.s)

                AppButton {
                    visible: !window.isInfo
                    text: window.isConfirm ? i18n.t("ssh.askpass_deny") : i18n.t("common.cancel")
                    onClicked: window.cancel()
                }

                AppButton {
                    primary: true
                    text: window.isConfirm ? i18n.t("ssh.askpass_allow") : i18n.t("ssh.askpass_ok")
                    onClicked: window.submit()
                }
            }
        }
    }

    Component.onCompleted: {
        if (isPassphrase)
            secretField.field.forceActiveFocus();
        else
            root.forceActiveFocus();
    }
}
