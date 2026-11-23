import QtQuick
import QtTest
import "qrc:/"

// The entries page is only loaded once a vault is open, so a mistake in it
// (an import missing, a type misspelled) would not show at start-up. It is
// compiled here, without the controller it needs to run.
TestCase {
    id: testCase
    name: "EntriesPage"
    when: windowShown
    visible: true
    width: 320
    height: 160

    ActionsMenu {
        id: menu
        options: ["Adicionar", "Remover", "Editar", "Copiar senha", "Configurar SSH", "Gerar senha"]
    }

    SignalSpy {
        id: menuChoice
        target: menu
        signalName: "chosen"
    }

    function test_page_compiles() {
        const component = Qt.createComponent("qrc:/EntriesPage.qml");
        compare(component.status, Component.Ready, component.errorString());
    }

    function test_context_menu_matches_the_selected_entry() {
        const component = Qt.createComponent("qrc:/EntriesPage.qml");
        const page = createTemporaryObject(component, testCase, {width: 320, height: 160});
        const list = findChild(page, "entryList");
        const actions = findChild(page, "entryActions");
        for (let index = 0; index < 2; ++index) {
            list.currentIndex = index;
            findChild(page, "entriesPane").leaveSearch();
            keyClick(Qt.Key_Space);
            tryCompare(actions, "opened", true);
            compare(actions.actions.includes("copyPassword"), index === 0);
            compare(actions.actions.includes("copyUsername"), index === 1);
            compare(actions.actions.includes("copyUrl"), index === 0);
            compare(actions.actions.includes("openUrl"), index === 0);
            compare(actions.actions.includes("copyOtp"), index === 1);
            verify(!actions.actions.includes("add") && !actions.actions.includes("generate"));
            keyClick(Qt.Key_Escape);
            tryCompare(actions, "opened", false);
        }
    }

    function test_click_outside_dismisses_without_choosing() {
        menuChoice.clear();
        menu.open();
        tryCompare(menu, "opened", true);
        mouseClick(testCase, 5, 5);
        tryCompare(menu, "opened", false);
        compare(menuChoice.count, 0);
    }

    function test_long_menu_keeps_keyboard_selection_visible() {
        menu.selected = 0;
        menuChoice.clear();
        menu.open();
        tryCompare(menu, "opened", true);
        compare(menu.height, 160);
        keyClick(Qt.Key_Up);
        compare(menu.selected, menu.options.length - 1);
        verify(menu.contentItem.contentY > 0);
        keyClick(Qt.Key_Return);
        compare(menuChoice.count, 1);
        compare(menuChoice.signalArguments[0][0], menu.options.length - 1);
        menu.close();
        tryCompare(menu, "opened", false);
    }
}
