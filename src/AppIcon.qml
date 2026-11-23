import QtQuick
import QtQuick.Shapes

// A line icon drawn on a 24 px grid, in any color and size, the way Hype and
// Omajim draw their toolbar icons (the paths they share are Omajim's).
Item {
    id: icon

    required property string name
    property color color: theme.foreground

    implicitWidth: 24
    implicitHeight: 24

    readonly property var paths: ({
        "plus": "M12 5V19 M5 12H19",
        "search": "M10.5 4A6.5 6.5 0 1 0 10.5 17A6.5 6.5 0 1 0 10.5 4 M15.5 15.5L20 20",
        "check": "M5 12.5L10 17.5L19 7",
        "close": "M6 6L18 18 M18 6L6 18",
        "chevron-right": "M9 6L15 12L9 18",
        "chevron-down": "M6 9L12 15L18 9",
        "keyboard": "M3 6H21V18H3Z M7 10H7.5 M11 10H11.5 M15 10H15.5 M7 14H17",
        "folder": "M3 7A1.5 1.5 0 0 1 4.5 5.5H9.5L11.5 7.5H19.5A1.5 1.5 0 0 1 21 9V17.5A1.5 1.5 0 0 1 19.5 19H4.5A1.5 1.5 0 0 1 3 17.5Z",
        "lock": "M6 11H18V20H6Z M8.5 11V7.5A3.5 3.5 0 0 1 15.5 7.5V11 M12 14.5V16.5",
        "key": "M8 12A4 4 0 1 0 8 20A4 4 0 1 0 8 12 M10.8 13.2L19.5 4.5 M15.5 8.5L18 11 M17.5 6.5L20 9",
        "settings": "M4 7H13 M17 7H20 M13 5V9 M4 17H7 M11 17H20 M7 15V19",
        "dice": "M6 4H18A2 2 0 0 1 20 6V18A2 2 0 0 1 18 20H6A2 2 0 0 1 4 18V6A2 2 0 0 1 6 4Z M8.5 8.5V8.6 M15.5 8.5V8.6 M12 12V12.1 M8.5 15.5V15.6 M15.5 15.5V15.6"
    })

    Shape {
        id: shape
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: Math.min(icon.width, icon.height) / 24
        antialiasing: true

        // The curve renderer (Qt 6.6 and later) draws the strokes sharp at
        // any size, as Omajim's icons are. Ubuntu LTS ships an older Qt
        // without it, so it is asked for only where it exists; there the
        // icon is drawn into a supersampled layer instead.
        Component.onCompleted: {
            if ("preferredRendererType" in shape) {
                shape.preferredRendererType = Shape.CurveRenderer;
            } else {
                shape.layer.textureSize = Qt.size(96, 96);
                shape.layer.smooth = true;
                shape.layer.samples = 4;
                shape.layer.enabled = true;
            }
        }

        ShapePath {
            fillColor: "transparent"
            strokeColor: icon.color
            strokeWidth: 1.8
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
