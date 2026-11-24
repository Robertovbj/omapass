import QtQuick
import QtQuick.Controls
import QtQuick.Window

ApplicationWindow {
    id: window

    width: 760
    height: 560
    minimumWidth: 460
    minimumHeight: 340
    visible: true
    title: "Omapass"

    // Every size in the interface is written against the desktop's text
    // scale, so the whole face grows with `omarchy display text size` the way
    // the rest of the family does.
    readonly property real s: systemTheme.textScale

    // Set by whichever page currently has a text field focused, so Ctrl+C
    // keeps its clipboard meaning while typing and only quits otherwise.
    property bool editingText: false

    // See-through under Omarchy's Glassy and Transparent windows.
    color: theme.windowBackground

    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: window.close()
    }

    Shortcut {
        sequence: "Ctrl+C"
        context: Qt.ApplicationShortcut
        enabled: !window.editingText && !window.settingsOpen
        onActivated: window.close()
    }

    // Settings live above the pages: they are the same wherever you are, and
    // the sheet has to survive the page changing under it.
    property bool settingsOpen: false

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        onActivated: window.settingsOpen = true
    }

    SettingsModal {
        visible: window.settingsOpen

        onSubmitted: function(values) {
            controller.saveSettings(values);
            window.settingsOpen = false;
        }
        onDismissed: window.settingsOpen = false
    }

    property bool updateBannerDismissed: false

    Rectangle {
        id: updateBanner
        visible: updater.supported && (updater.updateAvailable || updater.downloading)
                 && !window.updateBannerDismissed
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: visible ? Math.round(44 * window.s) : 0
        color: theme.panel
        z: 10

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.border
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Math.round(14 * window.s)
            anchors.rightMargin: Math.round(10 * window.s)
            spacing: Math.round(8 * window.s)

            Text {
                width: parent.width - bannerActions.width - Math.round(8 * window.s)
                anchors.verticalCenter: parent.verticalCenter
                text: updater.downloading
                    ? updater.statusText
                    : i18n.t("update.available", { "version": updater.latestVersion })
                color: updater.statusIsError ? theme.error : theme.foreground
                font.pixelSize: Math.round(13 * window.s)
                elide: Text.ElideRight
            }

            Row {
                id: bannerActions
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(6 * window.s)

                AppButton {
                    visible: updater.updateAvailable && !updater.downloading
                    primary: true
                    implicitHeight: Math.round(28 * window.s)
                    text: i18n.t("update.install_now")
                    onClicked: updater.installUpdate()
                }

                AppButton {
                    visible: !updater.downloading
                    flat: true
                    implicitHeight: Math.round(28 * window.s)
                    leftPadding: Math.round(8 * window.s)
                    rightPadding: Math.round(8 * window.s)
                    text: "×"
                    onClicked: window.updateBannerDismissed = true
                }
            }
        }
    }

    Loader {
        id: pageLoader
        anchors.top: updateBanner.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        function loadStage() {
            sourceComponent = null;
            sourceComponent = controller.stage === "databases" ? databasePage : entriesPage;
        }
        Component.onCompleted: loadStage()
        Connections {
            target: controller
            function onStageChanged() {
                window.settingsOpen = false;
                pageLoader.loadStage();
            }
        }
        // No forceActiveFocus() here: the page's own list pane claims the
        // keyboard when it should have it, and forcing focus onto the page
        // after loading would take it straight back off a sheet that opened
        // during construction.
        focus: true

        // A page loading (the list at start-up, the entries after an
        // unlock) takes the keyboard with it, even from a question the
        // browser left open over it.
        onLoaded: Qt.callLater(browserPrompt.refocus)
    }

    HintLayer { parent: window.Overlay.overlay }

    BrowserPromptModal {
        id: browserPrompt
    }

    // The browser asked to unlock, or asks something: the window comes up.
    Connections {
        target: controller.browser

        function onRaiseRequested() {
            if (window.visibility === Window.Minimized || window.visibility === Window.Hidden)
                window.showNormal();
            window.raise();
            window.requestActivate();
        }
    }

    Component {
        id: databasePage
        DatabasePage {}
    }

    Component {
        id: entriesPage
        EntriesPage {}
    }

    // Remember the last windowed geometry rather than whatever the window
    // happens to measure at teardown: a maximized window reports screen-sized
    // dimensions, and the close sequence hides the window before destruction,
    // so neither the live geometry nor the final visibility can be trusted.
    property rect normalGeometry: Qt.rect(x, y, width, height)
    property bool wasMaximized: false

    function trackNormalGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(x, y, width, height);
    }

    onXChanged: trackNormalGeometry()
    onYChanged: trackNormalGeometry()
    onWidthChanged: trackNormalGeometry()
    onHeightChanged: trackNormalGeometry()

    onVisibilityChanged: function(mode) {
        if (mode === Window.Maximized || mode === Window.FullScreen)
            wasMaximized = true;
        else if (mode === Window.Windowed)
            wasMaximized = false;
    }

    Component.onCompleted: {
        var geometry = controller.windowGeometry();
        if (geometry.valid) {
            x = geometry.x;
            y = geometry.y;
            width = geometry.width;
            height = geometry.height;
            if (geometry.maximized)
                showMaximized();
        } else {
            width = Math.round(760 * systemTheme.textScale);
            height = Math.round(560 * systemTheme.textScale);
        }
    }

    Component.onDestruction: controller.saveWindowGeometry(
        normalGeometry.x, normalGeometry.y,
        normalGeometry.width, normalGeometry.height, wasMaximized)
}
