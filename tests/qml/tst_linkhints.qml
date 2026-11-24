import QtQuick
import QtQuick.Controls
import QtTest
import "qrc:/"

TestCase {
    id: testCase
    name: "LinkHints"
    when: windowShown
    visible: true
    width: 720
    height: 500
    property int hits: 0

    Button { id: button; text: "A"; onClicked: testCase.hits++ }
    TextField { id: field; y: 60 }
    Repeater {
        id: extra
        model: 0
        Button {
            required property int index
            x: (index % 8) * 35
            y: 110 + Math.floor(index / 8) * 35
            width: 30
            height: 30
            onClicked: testCase.hits += index + 1
        }
    }
    HintLayer { id: hints; parent: testCase.Overlay.overlay }
    ActionsMenu {
        id: menu
        parent: testCase
        options: ["One", "Two"]
        onChosen: function(index) { testCase.hits += 10 + index; close(); }
    }

    function init() {
        button.forceActiveFocus();
        hits = 0;
    }
    function test_activate_cancel_and_type_in_field() {
        keyClick(Qt.Key_F);
        compare(linkHints.badges.length, 2);
        verify(hints.visible);
        const badges = hints.children.filter(item => item.modelData !== undefined);
        compare(badges.length, 2);
        compare(badges[1].y, field.mapToItem(hints, 0, 0).y);
        keyClick(Qt.Key_Escape);
        compare(linkHints.badges.length, 0);
        keyClick(Qt.Key_F);
        keyClick(linkHints.badges[0].label);
        compare(hits, 1);
        keyClick(Qt.Key_F);
        keyClick(linkHints.badges[1].label);
        verify(field.activeFocus);
        keyClick(Qt.Key_F);
        compare(field.text, "f");
        compare(linkHints.badges.length, 0);
    }
    SignalSpy { id: selected; target: controller; signalName: "databaseSelected" }
    SignalSpy { id: copied; target: controller; signalName: "passwordRequested" }

    function badgeAt(item) {
        const pos = item.mapToItem(hints, 0, 0);
        return hints.children.find(badge => badge.modelData !== undefined
                                   && Math.abs(badge.x - pos.x) < 1 && Math.abs(badge.y - pos.y) < 1);
    }

    function test_page_badges_data() {
        return [{tag: "databases", page: "DatabasePage"}, {tag: "entries", page: "EntriesPage"}];
    }
    function test_page_badges(data) {
        controller.emitDatabases([{path: "/test.kdbx", name: "Test", label: "Test", kind: "KeePassXC"},
                                  {path: "/other.kdbx", name: "Other", label: "Other", kind: "KeePassXC"}]);
        const component = Qt.createComponent("qrc:/" + data.page + ".qml");
        const page = createTemporaryObject(component, testCase, {width: width, height: height});
        verify(page);
        waitForRendering(page);
        const plus = findChild(page, "headerAction_plus");
        plus.forceActiveFocus();
        keyClick(Qt.Key_F);
        verify(badgeAt(plus));
        const databasePage = data.page === "DatabasePage";
        compare(!!badgeAt(findChild(page, "headerTitle")), !databasePage);
        const list = findChild(page, "entryList");
        list.currentIndex = 0;
        const badge = badgeAt(list.itemAtIndex(1));
        verify(badge);
        const spy = databasePage ? selected : copied;
        spy.clear();
        for (const letter of badge.modelData.label)
            keyClick(letter);
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], databasePage ? 1 : controller.entryPathAt(1));
    }

    Component {
        id: unlockDatabase
        UnlockModal { onDismissed: close() }
    }

    function test_unlock_starts_in_password_field() {
        const modal = createTemporaryObject(unlockDatabase, testCase);
        modal.open();
        tryCompare(modal.firstField, "activeFocus", true);
        keyClick(Qt.Key_F);
        compare(modal.firstField.text, "f");
        keyClick(Qt.Key_Escape);
        verify(!modal.firstField.activeFocus);
        verify(modal.opened);
        keyClick(Qt.Key_Escape);
        tryCompare(modal, "opened", false);
    }

    Component {
        id: createDatabase
        CreateDatabaseModal { onDismissed: close() }
    }

    function test_modal_hint_focus_and_escape() {
        const modal = createTemporaryObject(createDatabase, testCase);
        modal.open();
        tryCompare(modal, "opened", true);
        waitForRendering(modal.contentItem);
        keyClick(Qt.Key_F);
        const badge = linkHints.badges.find(item => {
            const point = modal.firstField.mapFromItem(hints, item.x, item.y);
            return modal.firstField.contains(point);
        });
        verify(badge);
        for (const letter of badge.label)
            keyClick(letter);
        verify(modal.firstField.activeFocus);
        keyClick(Qt.Key_F);
        compare(modal.firstField.text, "f");
        keyClick(Qt.Key_Escape);
        verify(!modal.firstField.activeFocus);
        verify(modal.opened);
        keyClick(Qt.Key_Escape);
        tryCompare(modal, "opened", false);
    }

    function test_type_a_two_letter_hint() {
        extra.model = 15;
        keyClick(Qt.Key_F);
        compare(linkHints.badges.length, 17);
        const label = linkHints.badges[16].label;
        compare(label.length, 2);
        keyClick(label[0]);
        compare(linkHints.prefix, label[0]);
        keyClick(Qt.Key_Backspace);
        compare(linkHints.prefix, "");
        keyClick(label[0]);
        keyClick(label[1]);
        compare(linkHints.badges.length, 0);
        compare(hits, 15);
        extra.model = 0;
    }
    function test_popup_limits_targets() {
        menu.open();
        tryCompare(menu, "opened", true);
        keyClick(Qt.Key_F);
        compare(linkHints.badges.length, 2);
        keyClick(linkHints.badges[1].label);
        tryCompare(testCase, "hits", 11);
    }
}
