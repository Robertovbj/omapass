import QtQuick
import QtTest
import "qrc:/"

// What the browser asks has to be answerable from the keyboard alone, and
// whatever closes the sheet — Esc, a click outside — has to refuse rather
// than leave the browser waiting.
TestCase {
    id: testCase
    name: "BrowserPromptModal"
    when: windowShown
    visible: true
    width: 700
    height: 700

    BrowserPromptModal {
        id: modal
    }

    readonly property var browser: controller.browser

    function ask(prompt) {
        browser.setPrompt(prompt);
        tryVerify(function() { return modal.opened; }, 3000);
    }

    function cleanup() {
        browser.setPrompt({});
        tryVerify(function() { return !modal.opened; }, 3000);
    }

    function lastAnswer() {
        compare(browser.answers.length, 1);
        return browser.answers[0];
    }

    function test_accessLetsOutWhatStaysTicked() {
        ask({ "kind": "access", "site": "github.com",
              "entries": [{ "path": "Web/GitHub", "name": "GitHub", "login": "ana" },
                          { "path": "Web/GitHub trabalho", "name": "GitHub trabalho", "login": "" }] });
        // Both entries start checked.
        const remember = findChild(modal.contentItem, "accessRemember");
        verify(remember.checked);
        verify(modal.accessOrder[0].checked && modal.accessOrder[1].checked);
        verify(!modal.accessOrder[0].activeFocus);
        keyClick(Qt.Key_Tab);
        verify(modal.accessOrder[0].activeFocus);

        keyClick(Qt.Key_Space);
        verify(!modal.accessOrder[0].checked);
        keyClick(Qt.Key_Tab);
        verify(modal.accessOrder[1].activeFocus);
        keyClick(Qt.Key_Tab);
        verify(remember.activeFocus);
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_Return);

        const answer = lastAnswer();
        compare(answer.kind, "access");
        compare(answer.value.allowed, ["Web/GitHub trabalho"]);
        compare(answer.value.remember, false);
        tryVerify(function() { return !modal.opened; }, 3000);
    }

    function test_escapeDeniesAccess() {
        ask({ "kind": "access", "site": "github.com",
              "entries": [{ "path": "Web/GitHub", "name": "GitHub", "login": "ana" }] });
        verify(!modal.accessOrder[0].activeFocus);
        keyClick(Qt.Key_Tab);
        verify(modal.accessOrder[0].activeFocus);
        keyClick(Qt.Key_Escape);
        verify(modal.opened);
        compare(browser.answers.length, 0);
        keyClick(Qt.Key_Escape);
        const answer = lastAnswer();
        compare(answer.value.allowed, []);
        compare(answer.value.remember, true);
    }

    function test_associateTakesAName() {
        ask({ "kind": "associate", "vault": "Pessoal.kdbx" });
        const name = findChild(modal.contentItem, "associateName");
        tryVerify(function() { return name.field.activeFocus; }, 2000);

        // Enter on an empty name does nothing; the browser keeps waiting.
        keyClick(Qt.Key_Return);
        compare(browser.answers.length, 0);

        keyClick(Qt.Key_N);
        keyClick(Qt.Key_B);
        keyClick(Qt.Key_Return);
        compare(lastAnswer().value, "nb");
    }

    function test_associateEscapeRefuses() {
        ask({ "kind": "associate", "vault": "Pessoal.kdbx" });
        const name = findChild(modal.contentItem, "associateName");
        tryVerify(function() { return name.field.activeFocus; }, 2000);
        keyClick(Qt.Key_Escape);
        verify(modal.opened);
        compare(browser.answers.length, 0);
        keyClick(Qt.Key_Escape);
        compare(lastAnswer().value, "");
    }

    function test_updateIsAYesOrNo() {
        ask({ "kind": "update", "name": "GitHub", "login": "ana", "site": "github.com" });
        keyClick(Qt.Key_Y);
        compare(lastAnswer().kind, "update");
        compare(lastAnswer().value, true);

        ask({ "kind": "conflict", "browsers": ["Brave"] });
        keyClick(Qt.Key_N);
        compare(lastAnswer().kind, "conflict");
        compare(lastAnswer().value, false);
    }

    Component {
        id: paneComponent
        ListPane {
            anchors.fill: parent
            model: [{ "path": "/casa/cofre.kdbx", "label": "/casa/cofre.kdbx" }]
        }
    }

    // At start-up the manifest question opens before the database list
    // loads, and the list used to take the keyboard from under it: y and n
    // went to the list's search line.
    function test_aPageLoadingUnderneathDoesNotTakeTheKeys() {
        ask({ "kind": "conflict", "browsers": ["Brave"] });
        const pane = createTemporaryObject(paneComponent, testCase);
        verify(pane !== null);
        pane.claimKeyboard();
        keyClick(Qt.Key_Y);
        compare(lastAnswer().kind, "conflict");
        compare(lastAnswer().value, true);
        // The question gone, the list has the keyboard again.
        tryVerify(function() { return pane.ownsKeys; }, 2000);
    }

    function test_clickingOutsideRefuses() {
        ask({ "kind": "update", "name": "GitHub", "login": "ana", "site": "github.com" });
        modal.dismissed();
        compare(lastAnswer().value, false);
    }
}
