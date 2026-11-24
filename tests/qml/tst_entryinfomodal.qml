import QtQuick
import QtTest
import "qrc:/"

// The details sheet: the login is shown along with the rest, and Enter on a
// field copies the whole of it — no need to drag over the text first.
TestCase {
    id: testCase
    name: "EntryInfoModal"
    when: windowShown
    visible: true
    width: 700
    height: 600

    EntryInfoModal {
        id: modal
        visible: false
        passBackend: false
        details: ({ "title": "Gmail", "username": "pessoa@exemplo.com",
                    "url": "https://mail.google.com", "notes": "segunda conta" })
    }

    Component {
        id: shortInfo
        EntryInfoModal { height: 360 }
    }

    function cleanup() {
        modal.visible = false;
        tryVerify(function() { return !modal.opened; }, 3000);
        modal.details = { "title": "Gmail", "username": "pessoa@exemplo.com",
                          "url": "https://mail.google.com", "notes": "segunda conta" };
    }

    function test_login_is_shown_and_enter_copies_it() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        keyClick(Qt.Key_Tab);

        const login = findChild(modal, "usernameField");
        verify(login !== null, "o campo de login existe");
        verify(login.visible);
        compare(login.value, "pessoa@exemplo.com");

        // Tab from the title lands on the login; Enter copies it.
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return login.view.activeFocus; }, 3000);
        keyClick(Qt.Key_Return);
        compare(controller.lastCopied, "pessoa@exemplo.com");
    }

    // Each additional URL is a field of its own after the main one: Tab
    // reaches it, Enter copies it and o opens it, like the main URL.
    function test_additional_urls_are_fields_of_their_own() {
        modal.details = { "title": "Sistema", "username": "ana", "url": "https://sistema.exemplo.com",
                          "extraUrls": ["http://localhost:8080", "http://192.168.0.10"], "notes": "" };
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        keyClick(Qt.Key_Tab);

        tryVerify(function() { return findChild(modal.contentItem, "extraUrlField1") !== null; }, 3000,
                  "um campo por URL adicional");
        const first = findChild(modal.contentItem, "extraUrlField0");
        const second = findChild(modal.contentItem, "extraUrlField1");
        compare(first.value, "http://localhost:8080");
        compare(second.value, "http://192.168.0.10");

        // Title, login, URL, then the additional ones.
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_O);
        compare(controller.lastOpened, "https://sistema.exemplo.com");
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return first.view.activeFocus; }, 3000);
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return second.view.activeFocus; }, 3000);
        keyClick(Qt.Key_Return);
        compare(controller.lastCopied, "http://192.168.0.10");
        keyClick(Qt.Key_O);
        compare(controller.lastOpened, "http://192.168.0.10");
        keyClick(Qt.Key_Backtab);
        tryVerify(function() { return first.view.activeFocus; }, 3000);
    }

    function test_enter_on_the_title_copies_the_title() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Return);
        compare(controller.lastCopied, "Gmail");
    }

    function test_entry_without_otp_hides_the_code() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        verify(!findChild(modal, "otpField").visible);
    }

    function test_wheel_over_notes_scrolls_the_text() {
        const info = createTemporaryObject(shortInfo, testCase, {
            details: { "title": "Gmail", "notes": "Linha de notas\n".repeat(30),
                       "extraUrls": ["https://example.com", "https://example.org"] }
        });
        info.open();
        tryCompare(info, "opened", true);

        const box = findChild(info, "notesField").scrollBox;
        const area = box.contentItem;
        const body = findChild(info, "modalBody").contentItem;
        verify(area.contentHeight > area.height);
        tryVerify(function() { return body.contentHeight > body.height + 24; });
        body.contentY = body.contentHeight - body.height - 24;
        const bodyY = body.contentY;
        mouseWheel(area, area.width / 2, area.height / 2, 0, -120);
        tryVerify(function() { return area.contentY > 0; });
        compare(body.contentY, bodyY);
    }

    function test_edit_button_opens_form_for_the_entry_shown() {
        const component = Qt.createComponent("qrc:/EntriesPage.qml");
        const page = createTemporaryObject(component, testCase, {width: 700, height: 600});
        verify(page);

        const list = findChild(page, "entryList");
        tryCompare(list, "count", 2);
        list.currentIndex = 0;
        page.openInfo();
        const info = findChild(page, "entryInfoModal");
        tryCompare(info, "opened", true);
        list.currentIndex = 1;

        const button = findChild(info, "modalAccept");
        compare(button.text, "Editar");
        mouseClick(button);
        tryCompare(info, "opened", false);
        const form = findChild(page, "entryFormModal");
        tryCompare(form, "opened", true);
        verify(form.isEdit);
        compare(form.fields.originalPath, "Trabalho/Gmail");
        compare(form.fields.notes, "segunda conta");
    }

    // The code sits between the URL and the notes in the Tab order, shows
    // the seconds left, and Enter copies it as a secret rather than as text.
    function test_otp_code_is_shown_and_enter_copies_it() {
        modal.details = { "title": "Gmail", "username": "pessoa@exemplo.com",
                          "url": "https://mail.google.com", "notes": "", "hasOtp": true };
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        keyClick(Qt.Key_Tab);

        const otp = findChild(modal, "otpField");
        verify(otp.visible);
        compare(otp.value, "287082");
        verify(otp.label.indexOf("4s") >= 0, otp.label);

        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Tab);
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return otp.view.activeFocus; }, 3000);
        keyClick(Qt.Key_Return);
        compare(controller.lastCopied, "otp:287082");
    }

    // A pass entry has only the title and, when pass-otp is in use, the code.
    function test_pass_entry_tabs_between_title_and_code() {
        modal.passBackend = true;
        modal.details = { "title": "gmail", "hasOtp": true };
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        keyClick(Qt.Key_Tab);

        const otp = findChild(modal, "otpField");
        verify(otp.visible);
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return otp.view.activeFocus; }, 3000);
        keyClick(Qt.Key_Tab);
        tryVerify(function() { return !otp.view.activeFocus; }, 3000);
        modal.passBackend = false;
    }

    function test_pass_entries_have_no_login() {
        modal.passBackend = true;
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        verify(!findChild(modal, "usernameField").visible);
        modal.passBackend = false;
    }
}
