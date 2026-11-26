import QtQuick
import QtTest
import "qrc:/"

// An entry does not need a group: with the field empty the groups are only
// offered, and Enter keeps the entry at the root. Where a group is required
// (1Password's vaults) the first one is still picked up front.
TestCase {
    id: testCase
    name: "EntryFormModal"
    when: windowShown
    visible: true
    width: 700
    height: 700

    EntryFormModal {
        id: modal
        visible: false
    }

    SignalSpy {
        id: saved
        target: modal
        signalName: "submitted"
    }

    function init() {
        controller.groups = ["Pessoal", "Trabalho"];
        saved.clear();
    }

    function cleanup() {
        modal.visible = false;
        tryVerify(function() { return !modal.opened; }, 3000);
        modal.groupRequired = false;
        controller.groups = [];
    }

    function open() {
        modal.isEdit = false;
        modal.fields = {};
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return modal.tabOrder[1].field.activeFocus; }, 3000,
                  "o teclado começa no grupo");
    }

    function test_enter_on_empty_group_keeps_the_root() {
        open();
        keyClick(Qt.Key_Return);
        compare(modal.tabOrder[1].text, "");
        tryVerify(function() { return modal.tabOrder[2].field.activeFocus; }, 3000,
                  "Enter segue para o título");

        keyClick(Qt.Key_G);
        keyClick(Qt.Key_Return);
        compare(saved.count, 1);
        compare(saved.signalArguments[0][0].group, "");
        compare(saved.signalArguments[0][0].title, "g");
    }

    function test_arrow_still_picks_a_group() {
        open();
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Return);
        compare(modal.tabOrder[1].text, "Pessoal");
    }

    function test_typed_group_still_completes() {
        open();
        keyClick(Qt.Key_T);
        keyClick(Qt.Key_Return);
        compare(modal.tabOrder[1].text, "Trabalho");
    }

    function test_required_group_is_preselected() {
        modal.groupRequired = true;
        open();
        keyClick(Qt.Key_Return);
        compare(modal.tabOrder[1].text, "Pessoal");
    }
}
