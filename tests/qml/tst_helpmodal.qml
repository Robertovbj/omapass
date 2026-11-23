import QtQuick
import QtTest
import "qrc:/"

// The shortcut list filters as you type: a shortcut stays when its keys, its
// description or its group mention every word, and empty groups go.
TestCase {
    id: testCase
    name: "HelpModal"
    when: windowShown
    visible: true
    width: 900
    height: 700

    HelpModal {
        id: modal
        visible: false
        sections: [
            { "title": "Ações", "items": [["Enter", "Copiar senha"], ["Ctrl+B", "Copiar login"]] },
            { "title": "Navegação", "items": [["j / k", "Mover seleção"]] }
        ]
    }

    function test_typing_filters_the_shortcuts() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        const filter = findChild(modal.contentItem, "shortcutFilter");
        tryVerify(function() { return filter.field.activeFocus; }, 3000, "o filtro começa com o teclado");

        compare(modal.shownSections.length, 2);
        keyClick(Qt.Key_L);
        keyClick(Qt.Key_O);
        keyClick(Qt.Key_G);
        compare(modal.shownSections.length, 1);
        compare(modal.shownSections[0].items.length, 1);
        compare(modal.shownSections[0].items[0][0], "Ctrl+B");

        filter.text = "ctrl+b copiar";
        compare(modal.shownSections.length, 1, "todas as palavras precisam aparecer");
        filter.text = "navegação";
        compare(modal.shownSections[0].title, "Navegação", "o nome do grupo também conta");
        filter.text = "nada disso";
        compare(modal.shownSections.length, 0);
    }
}
