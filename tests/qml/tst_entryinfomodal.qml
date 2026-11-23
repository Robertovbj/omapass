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

    function cleanup() {
        modal.visible = false;
        tryVerify(function() { return !modal.opened; }, 3000);
        modal.details = { "title": "Gmail", "username": "pessoa@exemplo.com",
                          "url": "https://mail.google.com", "notes": "segunda conta" };
    }

    function test_login_is_shown_and_enter_copies_it() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);

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

    function test_enter_on_the_title_copies_the_title() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        keyClick(Qt.Key_Return);
        compare(controller.lastCopied, "Gmail");
    }

    function test_entry_without_otp_hides_the_code() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);
        verify(!findChild(modal, "otpField").visible);
    }

    // The code sits between the URL and the notes in the Tab order, shows
    // the seconds left, and Enter copies it as a secret rather than as text.
    function test_otp_code_is_shown_and_enter_copies_it() {
        modal.details = { "title": "Gmail", "username": "pessoa@exemplo.com",
                          "url": "https://mail.google.com", "notes": "", "hasOtp": true };
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);

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
