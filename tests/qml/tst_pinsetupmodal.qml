import QtQuick
import QtTest
import "qrc:/"

// Setting a PIN up: only digits go into the PIN until letters and symbols
// are allowed, and taking that back leaves only the digits typed.
TestCase {
    id: testCase
    name: "PinSetupModal"
    when: windowShown
    visible: true
    width: 700
    height: 600

    PinSetupModal {
        id: modal
        visible: false
        busy: false
    }

    function cleanup() {
        modal.visible = false;
        tryVerify(function() { return !modal.opened; }, 3000);
    }

    function test_only_digits_until_letters_are_allowed() {
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);

        const pin = findChild(modal.contentItem, "pinField");
        const allow = findChild(modal.contentItem, "pinAllowText");
        verify(pin !== null && allow !== null);

        pin.field.forceActiveFocus();
        keyClick(Qt.Key_1);
        keyClick(Qt.Key_A);
        keyClick(Qt.Key_2);
        compare(pin.text, "12", "letras não entram");

        allow.checked = true;
        pin.field.forceActiveFocus();
        keyClick(Qt.Key_B);
        compare(pin.text, "12b", "com letras permitidas, entram");

        allow.checked = false;
        compare(pin.text, "12", "desmarcar deixa só os dígitos");
    }
}
