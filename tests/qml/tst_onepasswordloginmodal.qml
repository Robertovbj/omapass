import QtQuick
import QtTest
import "qrc:/"

// Adding a 1Password account. When `op` asks for the two-step code, the
// field it is asked in has to take the keyboard by itself — the step, the
// busy flag and the sheet's own opening land in whatever order they land,
// and the code field used to be left unfocused.
TestCase {
    id: testCase
    name: "OnePasswordLoginModal"
    when: windowShown
    visible: true
    width: 700
    height: 500

    // The sheet centres itself on the window's overlay, like it does in the
    // application; anchoring it here would fight that.
    OnePasswordLoginModal {
        id: modal
        visible: false
        busy: false
    }

    function test_code_field_takes_the_keyboard() {
        modal.step = "opCredentials";
        modal.visible = true;
        tryVerify(function() { return modal.opened; }, 3000);

        verify(!findChild(modal, "emailField").field.activeFocus);

        // What the controller does when op asks for the code.
        modal.busy = true;
        modal.step = "opCode";
        modal.busy = false;

        const code = findChild(modal, "codeField");
        tryVerify(function() { return code.field.focus; }, 3000);
    }

    function test_address_comes_filled_in() {
        compare(modal.address, "my.1password.com");
    }
}
