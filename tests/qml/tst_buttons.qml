import QtQuick
import QtTest
import "qrc:/"

// The buttons light up under the pointer whatever the platform theme says:
// with Omarchy's qtengine theme Qt reports no hover effects, and a button
// left to that default never learns it is hovered.
TestCase {
    id: testCase
    name: "Buttons"
    when: windowShown
    visible: true
    width: 300
    height: 200

    IconButton {
        id: iconButton
        x: 10
        y: 10
        iconName: "dice"
        description: "Gerar outra"
    }

    AppButton {
        id: textButton
        x: 10
        y: 80
        text: "Copiar"
    }

    function test_buttons_light_up_under_the_pointer() {
        const buttons = [iconButton, textButton];
        for (let i = 0; i < buttons.length; ++i) {
            const button = buttons[i];
            verify(button.hoverEnabled, "hover pedido explicitamente");
            mouseMove(testCase, 290, 190);
            verify(!button.lit);
            mouseMove(button, button.width / 2, button.height / 2);
            tryVerify(function() { return button.hovered && button.lit; }, 3000,
                      "acende sob o ponteiro");
        }
        mouseMove(testCase, 290, 190);
    }
}
