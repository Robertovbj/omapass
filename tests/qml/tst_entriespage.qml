import QtQuick
import QtTest

// The entries page is only loaded once a vault is open, so a mistake in it
// (an import missing, a type misspelled) would not show at start-up. It is
// compiled here, without the controller it needs to run.
TestCase {
    name: "EntriesPage"

    function test_page_compiles() {
        const component = Qt.createComponent("qrc:/EntriesPage.qml");
        compare(component.status, Component.Ready, component.errorString());
    }
}
