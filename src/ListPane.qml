import QtQuick
import QtQuick.Controls

// The header, the search box, the list and the status line (the shape both
// stages of omapass share, laid out like Omajim's window) together with the
// whole keyboard vocabulary. The pages above decide what each gesture means;
// nothing here knows about vaults.
FocusScope {
    id: pane

    property string searchPlaceholder: ""
    property var model: []
    // The header: what is open (centre) and the actions offered as icons
    // (right), each { "icon", "description", "action": function }.
    property bool hasHeadingMenu: false
    signal headingMenuRequested(real x, real y)
    property string heading: ""
    property string subheading: ""
    property var headerActions: []
    // The status line: what just happened on the left (a copy, an error),
    // a reminder of where the shortcuts are on the right.
    property string footerText: ""
    property color footerColor: theme.muted
    property string footerHint: ""
    property bool searchMode: true
    property bool acceptsKeys: true
    // A question from the browser sits over every page, whatever the page
    // thinks is on top: while one is up, the keyboard is the question's.
    readonly property bool browserAsking: controller.browser.prompt.kind !== undefined
    readonly property bool ownsKeys: acceptsKeys && !browserAsking
    // Tag drawn as a badge before a row's text, or "" for none.
    property var tagFor: function(item) { return ""; }
    // Optional table layout: [{ "key": "name", "title": "Nome", "weight": 3 }…].
    // Each row shows modelData[key] in the column; without columns a row is
    // the single text line the database list uses.
    property var columns: []
    readonly property bool tabular: columns.length > 0
    readonly property real columnSpacing: Math.round(12 * s)
    readonly property real columnWeightTotal: {
        let total = 0;
        for (let i = 0; i < columns.length; ++i)
            total += columns[i].weight;
        return total;
    }
    // A column marked `"fit": true` takes the room its header and widest
    // value need, never more than its weight would give it; the others share
    // what is left. An empty group column shrinks to its title.
    function longestIn(key) {
        const longest = model ? model.longest : undefined;
        if (longest !== undefined)
            return String(longest[key] || "");
        let result = "";
        if (Array.isArray(model)) {
            for (const row of model) {
                const text = row && row[key] !== undefined && row[key] !== null ? String(row[key]) : "";
                if (text.length > result.length)
                    result = text;
            }
        }
        return result;
    }
    function fitWidth(i, share) {
        const column = columns[i];
        const header = headerMetrics.advanceWidth(column.title) + headerMetrics.font.pixelSize;
        const cell = cellMetrics.advanceWidth(longestIn(column.key)) + cellMetrics.font.pixelSize / 2;
        return Math.min(share, Math.ceil(Math.max(header, cell)));
    }
    // Width of column `i` inside a row `rowWidth` wide (margins included).
    function columnWidth(i, rowWidth) {
        const room = rowWidth - Math.round(20 * s) - columnSpacing * (columns.length - 1);
        let fixed = 0;
        let flexWeight = 0;
        for (let c = 0; c < columns.length; ++c) {
            if (columns[c].fit)
                fixed += fitWidth(c, room * columns[c].weight / columnWeightTotal);
            else
                flexWeight += columns[c].weight;
        }
        if (columns[i].fit)
            return Math.max(0, fitWidth(i, room * columns[i].weight / columnWeightTotal));
        return Math.max(0, (room - fixed) * columns[i].weight / flexWeight);
    }

    FontMetrics {
        id: cellMetrics
        font.pixelSize: Math.round(14 * pane.s)
    }
    FontMetrics {
        id: headerMetrics
        font.pixelSize: Math.round(12 * pane.s)
        font.weight: Font.DemiBold
    }

    property alias query: searchInput.text
    property alias currentIndex: list.currentIndex
    readonly property bool typing: searchInput.activeFocus

    signal activated(int index)
    signal detailsRequested()
    signal menuRequested(real menuX, real menuY)
    signal addRequested()
    signal editRequested()
    signal deleteRequested()
    signal generateRequested()
    signal sendRequested()
    signal pinToggleRequested()
    signal copyUsernameRequested()
    signal copyOtpRequested()
    signal copyUrlRequested()
    signal openUrlRequested()
    signal helpRequested()
    signal quitRequested()

    readonly property real s: systemTheme.textScale
    readonly property int rowHeight: Math.round(34 * s)

    property bool lastKeyWasG: false

    function next() {
        if (list.count === 0)
            return;
        list.currentIndex = list.currentIndex >= list.count - 1 ? 0 : list.currentIndex + 1;
    }

    function previous() {
        if (list.count === 0)
            return;
        list.currentIndex = list.currentIndex <= 0 ? list.count - 1 : list.currentIndex - 1;
    }

    function goToTop() {
        if (list.count > 0)
            list.currentIndex = 0;
    }

    function goToBottom() {
        if (list.count > 0)
            list.currentIndex = list.count - 1;
    }

    function halfPage() {
        return Math.max(1, Math.floor(list.height / pane.rowHeight / 2));
    }

    function halfPageDown() {
        if (list.count > 0)
            list.currentIndex = Math.min(list.count - 1, list.currentIndex + halfPage());
    }

    function halfPageUp() {
        if (list.count > 0)
            list.currentIndex = Math.max(0, list.currentIndex - halfPage());
    }

    function enterSearch() {
        pane.searchMode = true;
        searchInput.forceActiveFocus();
    }

    function leaveSearch() {
        pane.searchMode = false;
        keyCatcher.forceActiveFocus();
    }

    // Chords that work the same whether or not the search line has focus.
    function handleControlKey(event) {
        if (!(event.modifiers & Qt.ControlModifier))
            return false;

        switch (event.key) {
        case Qt.Key_Tab:
            if (!pane.hasHeadingMenu)
                return false;
            pane.headingMenuRequested(pane.width / 2, headerBar.height);
            return true;
        case Qt.Key_D: halfPageDown(); return true;
        case Qt.Key_U: halfPageUp(); return true;
        case Qt.Key_N: next(); return true;
        case Qt.Key_P: previous(); return true;
        case Qt.Key_A: pane.addRequested(); return true;
        case Qt.Key_E: pane.editRequested(); return true;
        case Qt.Key_X: pane.deleteRequested(); return true;
        case Qt.Key_G: pane.generateRequested(); return true;
        case Qt.Key_S:
            if (event.modifiers & Qt.ShiftModifier) {
                pane.sendRequested();
                return true;
            }
            return false;
        case Qt.Key_I: pane.pinToggleRequested(); return true;
        // B for the login as in KeePassXC; U, its key for the URL, already
        // scrolls half a page here, so L (link) copies it and Shift+L opens
        // it. Ctrl+O is taken by the settings, application-wide.
        case Qt.Key_B: pane.copyUsernameRequested(); return true;
        // T for the one-time password, as in KeePassXC.
        case Qt.Key_T: pane.copyOtpRequested(); return true;
        case Qt.Key_L:
            if (event.modifiers & Qt.ShiftModifier)
                pane.openUrlRequested();
            else
                pane.copyUrlRequested();
            return true;
        // Ctrl+? reaches applications as Ctrl+Shift+/ or plain Ctrl+/
        // depending on the layout; both open the shortcut sheet.
        case Qt.Key_Question:
        case Qt.Key_Slash:
            pane.helpRequested();
            return true;
        }
        return false;
    }

    // Anchored rather than laid out in a Column: the list has to take the
    // space the search line and the footer leave over, which a column cannot
    // express without the list's height depending on its own position.
    Item {
        id: content
        anchors.fill: parent
        anchors.leftMargin: Math.round(20 * pane.s)
        anchors.rightMargin: Math.round(20 * pane.s)
        anchors.bottomMargin: Math.round(10 * pane.s)

        Item {
            id: headerBar
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Math.round(58 * pane.s)

            Text {
                id: appName
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Omapass"
                color: theme.accent
                font.pixelSize: Math.round(22 * pane.s)
                font.weight: Font.Bold
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: appName.right
                anchors.right: actionsRow.left
                anchors.leftMargin: Math.round(8 * pane.s)
                anchors.rightMargin: Math.round(8 * pane.s)
                spacing: Math.round(2 * pane.s)

                Text {
                    objectName: "headerTitle"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, parent.width)
                    visible: text.length > 0
                    text: pane.hasHeadingMenu ? pane.heading + " ▾" : pane.heading
                    color: theme.foreground
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    font.pixelSize: Math.round(13 * pane.s)
                    font.weight: Font.DemiBold
                    MouseArea {
                        anchors.fill: parent
                        visible: pane.hasHeadingMenu
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const pos = mapToItem(pane, width / 2, height);
                            pane.headingMenuRequested(pos.x, pos.y);
                        }
                    }
                }

                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: pane.subheading
                    color: theme.muted
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(12 * pane.s)
                }
            }

            Row {
                id: actionsRow
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Math.round(2 * pane.s)

                Repeater {
                    model: pane.headerActions

                    IconButton {
                        required property var modelData
                        objectName: "headerAction_" + modelData.icon
                        iconName: modelData.icon
                        description: modelData.description
                        onClicked: modelData.action()
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: theme.border
            }
        }

        Item {
            id: searchRow
            anchors.top: headerBar.bottom
            anchors.topMargin: Math.round(14 * pane.s)
            anchors.left: parent.left
            anchors.right: parent.right
            height: Math.round(40 * pane.s)

            TextField {
                id: searchInput
                anchors.fill: parent
                placeholderText: pane.searchPlaceholder
                placeholderTextColor: theme.muted
                color: theme.foreground
                font.pixelSize: Math.round(14 * pane.s)
                selectionColor: theme.selection
                selectedTextColor: theme.selectionText
                leftPadding: Math.round(40 * pane.s)
                rightPadding: Math.round(12 * pane.s)
                background: Rectangle {
                    color: theme.background
                    radius: Math.min(4, theme.radius)
                    border.width: 1
                    border.color: searchInput.activeFocus ? theme.accent : theme.border

                    AppIcon {
                        anchors.left: parent.left
                        anchors.leftMargin: Math.round(12 * pane.s)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.round(18 * pane.s)
                        height: width
                        name: "search"
                        color: searchInput.activeFocus ? theme.foreground : theme.muted
                    }
                }

                Keys.onPressed: function(event) {
                    if (!pane.ownsKeys)
                        return;
                    if (pane.handleControlKey(event)) {
                        event.accepted = true;
                        return;
                    }
                    if (event.modifiers & Qt.ControlModifier)
                        return;

                    switch (event.key) {
                    case Qt.Key_Escape:
                        pane.leaveSearch();
                        break;
                    case Qt.Key_Down:
                        pane.next();
                        break;
                    case Qt.Key_Up:
                        pane.previous();
                        break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        pane.activated(list.currentIndex);
                        break;
                    case Qt.Key_Tab:
                        pane.detailsRequested();
                        break;
                    default:
                        return;
                    }
                    event.accepted = true;
                }
            }
        }

        Item {
            id: listArea
            anchors.top: searchRow.bottom
            anchors.bottom: footer.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: Math.round(12 * pane.s)
            anchors.bottomMargin: Math.round(8 * pane.s)

            // One handler for the whole list area, rows included: the empty
            // part has to open the menu as well (with nothing in the list
            // there is no row to aim at), and a handler per row would fire
            // alongside this one and ask for the menu twice.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onSingleTapped: function(eventPoint) {
                    const inList = listArea.mapToItem(list, eventPoint.position.x,
                                                      eventPoint.position.y);
                    const index = list.indexAt(list.contentX + inList.x, list.contentY + inList.y);
                    if (index >= 0)
                        list.currentIndex = index;

                    const scenePoint = listArea.mapToItem(pane, eventPoint.position.x,
                                                          eventPoint.position.y);
                    pane.menuRequested(scenePoint.x, scenePoint.y);
                }
            }

            Row {
                id: header
                visible: pane.tabular
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Math.round(10 * pane.s)
                height: pane.tabular ? pane.rowHeight : 0
                spacing: pane.columnSpacing

                Repeater {
                    model: pane.columns
                    Text {
                        objectName: "columnHeader"
                        width: pane.columnWidth(index, listArea.width)
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.title
                        color: theme.muted
                        elide: Text.ElideRight
                        font.pixelSize: Math.round(12 * pane.s)
                        font.weight: Font.DemiBold
                    }
                }
            }

            ListView {
                id: list
                objectName: "entryList"
                anchors.top: header.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                model: pane.model
                clip: true
                keyNavigationEnabled: false
                highlightMoveDuration: 0
                currentIndex: 0
                boundsBehavior: Flickable.StopAtBounds

                // Re-filtering always lands on the first match, as the TUI
                // did on every keystroke. Deferred because `count` still
                // reflects the previous model while this fires.
                onModelChanged: Qt.callLater(function() {
                    list.currentIndex = list.count > 0 ? 0 : -1;
                })

                ScrollBar.vertical: AppScrollBar { objectName: "listScrollBar"; view: list }

                // A notch goes 120 px, and further when turned fast, instead
                // of the few rows Flickable's own wheel moves.
                SmoothWheel {
                    view: list
                }

                delegate: Rectangle {
                    id: row
                    width: list.width
                    height: pane.rowHeight
                    radius: Math.min(4, theme.radius)
                    color: row.current ? theme.selection
                         : hover.hovered ? theme.hover
                         : "transparent"

                    readonly property bool current: list.currentIndex === index

                    readonly property string itemText: typeof modelData === "string" ? modelData
                        : modelData.label !== undefined ? modelData.label
                        : modelData.path !== undefined ? modelData.path : ""
                    readonly property string tag: pane.tagFor(modelData)

                    function activateHint() {
                        list.currentIndex = index;
                        pane.activated(index);
                    }

                    function cell(key) {
                        const value = typeof modelData === "object" ? modelData[key] : "";
                        return value === undefined || value === null ? "" : String(value);
                    }

                    HoverHandler {
                        id: hover
                    }

                    Row {
                        visible: pane.tabular
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Math.round(10 * pane.s)
                        spacing: pane.columnSpacing

                        Repeater {
                            model: pane.columns
                            Item {
                                id: cellItem
                                width: pane.columnWidth(index, row.width)
                                height: cellText.implicitHeight
                                anchors.verticalCenter: parent ? parent.verticalCenter : undefined

                                // The name leads with the site's icon, when
                                // there is one; the type column stays as it
                                // was for the rest.
                                readonly property string icon: modelData.key === "name" ? row.cell("icon") : ""

                                Image {
                                    id: cellIcon
                                    objectName: "rowIcon"
                                    visible: cellItem.icon.length > 0 && status === Image.Ready
                                    source: cellItem.icon
                                    width: Math.round(16 * pane.s)
                                    height: width
                                    sourceSize: Qt.size(32, 32)
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    smooth: true
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    id: cellText
                                    objectName: "cell_" + modelData.key
                                    x: cellIcon.visible ? cellIcon.width + Math.round(6 * pane.s) : 0
                                    width: Math.max(0, parent.width - x - (otpBadge.visible ? otpBadge.width + Math.round(6 * pane.s) : 0))
                                    text: row.cell(modelData.key)
                                    color: row.current ? theme.selectionText
                                         : modelData.key === "name" ? theme.foreground : theme.muted
                                    opacity: row.current && modelData.key !== "name" ? 0.8 : 1
                                    elide: Text.ElideRight
                                    font.pixelSize: Math.round(14 * pane.s)
                                    font.weight: modelData.key === "name" ? Font.Medium : Font.Normal
                                }

                                Rectangle {
                                    id: otpBadge
                                    objectName: "rowOtpBadge"
                                    visible: modelData.key === "name" && !!(row.modelData && row.modelData.hasOtp)
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: otpBadgeText.implicitWidth + Math.round(10 * pane.s)
                                    height: Math.round(18 * pane.s)
                                    radius: Math.min(3, theme.radius)
                                    color: "transparent"
                                    border.width: 1
                                    border.color: row.current
                                        ? Qt.rgba(theme.selectionText.r, theme.selectionText.g, theme.selectionText.b, 0.5)
                                        : theme.accent

                                    Text {
                                        id: otpBadgeText
                                        anchors.centerIn: parent
                                        text: "OTP"
                                        color: row.current ? theme.selectionText : theme.accent
                                        font.pixelSize: Math.round(10 * pane.s)
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }

                    Row {
                        visible: !pane.tabular
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Math.round(10 * pane.s)
                        anchors.rightMargin: Math.round(10 * pane.s)
                        spacing: Math.round(8 * pane.s)

                        Rectangle {
                            id: tagBadge
                            visible: row.tag.length > 0
                            width: tagText.implicitWidth + Math.round(14 * pane.s)
                            height: Math.round(20 * pane.s)
                            radius: Math.min(3, theme.radius)
                            color: "transparent"
                            border.width: 1
                            border.color: row.current ? Qt.rgba(theme.selectionText.r, theme.selectionText.g,
                                                                          theme.selectionText.b, 0.4) : theme.border
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                id: tagText
                                objectName: "rowTag"
                                anchors.centerIn: parent
                                text: row.tag
                                color: row.current ? theme.selectionText : theme.muted
                                font.pixelSize: Math.round(11 * pane.s)
                            }
                        }

                        Text {
                            objectName: "rowText"
                            width: parent.width - (row.tag.length > 0 ? x : 0)
                            text: row.itemText
                            color: row.current ? theme.selectionText : theme.foreground
                            elide: Text.ElideMiddle
                            font.pixelSize: Math.round(14 * pane.s)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onSingleTapped: {
                            list.currentIndex = index;
                            pane.leaveSearch();
                        }
                        onDoubleTapped: row.activateHint()
                    }

                }
            }
        }

        Item {
            id: footer
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: Math.max(footerMessage.implicitHeight, footerHintText.implicitHeight)

            Text {
                id: footerMessage
                objectName: "footerText"
                anchors.left: parent.left
                anchors.right: footerHintText.left
                anchors.rightMargin: Math.round(16 * pane.s)
                anchors.verticalCenter: parent.verticalCenter
                text: pane.footerText
                color: pane.footerColor
                elide: Text.ElideRight
                font.pixelSize: Math.round(12 * pane.s)
            }

            Text {
                id: footerHintText
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: pane.footerHint
                color: theme.muted
                font.pixelSize: Math.round(12 * pane.s)
            }
        }
    }

    // Holds focus whenever the search line does not, so the vim-style keys
    // reach the pane instead of being typed into the query.
    Item {
        id: keyCatcher
        anchors.fill: parent
        focus: !pane.searchMode
        z: -1

        Keys.onPressed: function(event) {
            if (!pane.ownsKeys)
                return;
            if (pane.handleControlKey(event)) {
                event.accepted = true;
                pane.lastKeyWasG = false;
                return;
            }
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
                return;

            var wasG = false;
            switch (event.key) {
            case Qt.Key_Q:
            case Qt.Key_Escape:
                pane.quitRequested();
                break;
            case Qt.Key_J:
            case Qt.Key_Down:
                pane.next();
                break;
            case Qt.Key_K:
            case Qt.Key_Up:
                pane.previous();
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
                pane.activated(list.currentIndex);
                break;
            case Qt.Key_Tab:
                pane.detailsRequested();
                break;
            case Qt.Key_Space:
                pane.menuRequested(-1, -1);
                break;
            case Qt.Key_Slash:
            case Qt.Key_I:
                pane.enterSearch();
                break;
            // ?, as in Omarchy's other apps; Ctrl+? works from the search too.
            case Qt.Key_Question:
                pane.helpRequested();
                break;
            case Qt.Key_G:
                if (event.modifiers & Qt.ShiftModifier) {
                    pane.goToBottom();
                } else if (pane.lastKeyWasG) {
                    pane.goToTop();
                } else {
                    wasG = true;
                }
                break;
            default:
                return;
            }

            pane.lastKeyWasG = wasG;
            event.accepted = true;
        }
    }

    // The pane only ever takes the keyboard while the page says it may: a
    // sheet on top owns it otherwise, and grabbing focus regardless would
    // send typing to the search line behind the sheet.
    function claimKeyboard() {
        if (!pane.ownsKeys)
            return;
        if (pane.searchMode)
            searchInput.forceActiveFocus();
        else
            keyCatcher.forceActiveFocus();
    }

    onOwnsKeysChanged: claimKeyboard()

    Component.onCompleted: claimKeyboard()
}
