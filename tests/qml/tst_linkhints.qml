import QtQuick
import QtQuick.Controls
import QtTest
import "qrc:/"

TestCase {
    id: testCase
    name: "LinkHints"
    when: windowShown
    visible: true
    width: 320
    height: 240
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
