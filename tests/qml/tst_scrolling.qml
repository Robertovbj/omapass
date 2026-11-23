import QtQuick
import QtTest
import "qrc:/"

// Scrolling as in Omajim: a wheel notch moves the list a good way (90 px,
// not the few rows Flickable's own wheel moves), and at the end of a
// sheet's body the scroll stops there instead of going past and back.
TestCase {
    id: testCase
    name: "Scrolling"
    when: windowShown
    visible: true
    width: 700
    height: 420

    function rows(n) {
        const result = [];
        for (let i = 0; i < n; ++i)
            result.push({ "path": "g/e" + i, "name": "Entrada " + i, "group": "g" });
        return result;
    }

    ListPane {
        id: pane
        anchors.fill: parent
        model: testCase.rows(200)
        columns: [{ "key": "name", "title": "Nome", "weight": 1 }]
    }

    EntryFormModal {
        id: form
        visible: false
    }

    function cleanup() {
        form.visible = false;
        tryVerify(function() { return !form.opened; }, 3000);
    }

    function listView() {
        return findChild(pane, "entryList");
    }

    function test_a_notch_moves_the_list_a_good_way() {
        const list = listView();
        verify(list !== null);
        list.contentY = 0;
        mouseWheel(list, list.width / 2, list.height / 2, 0, -120);
        tryVerify(function() { return list.contentY >= 89; }, 2000,
                  "um entalhe desce 90 px, não poucas linhas");
    }

    // The wheel moves the list by its position, which Flickable does not
    // call moving: the bar shows all the same, and goes again after.
    function test_the_bar_shows_while_the_wheel_scrolls() {
        const list = listView();
        list.contentY = 0;
        const bar = findChild(pane, "listScrollBar");
        verify(bar !== null);
        tryVerify(function() { return !bar.active; }, 3000);
        mouseWheel(list, list.width / 2, list.height / 2, 0, -120);
        tryVerify(function() { return bar.active; }, 1000, "a barra aparece ao rolar");
        tryVerify(function() { return !bar.active; }, 3000, "e some depois");
    }

    function test_the_list_stops_at_its_end() {
        const list = listView();
        for (let i = 0; i < 80; ++i)
            mouseWheel(list, list.width / 2, list.height / 2, 0, -120);
        const end = list.originY + list.contentHeight - list.height;
        tryVerify(function() { return Math.abs(list.contentY - end) < 1; }, 4000);
        wait(300);
        verify(list.contentY <= end + 0.5, "não passa do fim");
    }

    function test_a_sheet_stops_at_the_end_of_its_body() {
        form.visible = true;
        tryVerify(function() { return form.opened; }, 3000);
        const flick = findChild(form.contentItem, "modalBody").contentItem;
        tryVerify(function() { return flick.contentHeight > flick.height; }, 3000,
                  "o formulário rola nesta janela");
        verify(flick.contentHeight > flick.height, "o formulário rola nesta janela");

        let furthest = 0;
        for (let i = 0; i < 30; ++i) {
            mouseWheel(flick, flick.width / 2, flick.height / 2, 0, -120);
            furthest = Math.max(furthest, flick.contentY);
            wait(16);
        }
        const end = flick.contentHeight - flick.height;
        tryVerify(function() { return Math.abs(flick.contentY - end) < 1; }, 3000);
        verify(furthest <= end + 0.5, "nunca passa da borda para voltar depois");
    }
}
