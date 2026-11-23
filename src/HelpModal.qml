import QtQuick

// Every shortcut, grouped, in the layout of Hype's overlay, with a filter
// over them: opened with Ctrl+? or ?, closed with Esc. Each page supplies its
// own sections as [{ title, items: [[keys, description], …] }].
Modal {
    id: root

    property var sections: []

    // The sections with only the shortcuts whose keys, description or
    // group mention every word typed in the filter.
    readonly property var shownSections: {
        const words = filterField.text.toLowerCase().split(/\s+/).filter(function(w) { return w.length > 0; });
        if (words.length === 0)
            return root.sections;
        const matches = function(text) {
            const haystack = text.toLowerCase();
            return words.every(function(word) { return haystack.indexOf(word) >= 0; });
        };
        const result = [];
        for (const section of root.sections) {
            const items = section.items.filter(function(item) {
                return matches(item[0] + " " + item[1] + " " + section.title);
            });
            if (items.length > 0)
                result.push({ "title": section.title, "items": items });
        }
        return result;
    }

    capturesKeys: false

    onShown: {
        filterField.text = "";
        filterField.field.forceActiveFocus();
    }

    heading: i18n.t("common.shortcuts_title")
    cardWidth: Math.round(820 * s)

    Field {
        id: filterField
        objectName: "shortcutFilter"
        width: parent.width
        placeholderText: i18n.t("help.filter")

        onCancelled: root.dismissed()
    }

    // ? and q are typed into the filter; F1 still closes.
    Shortcut {
        sequence: "F1"
        enabled: root.visible
        onActivated: root.dismissed()
    }

    Text {
        width: parent.width
        visible: root.shownSections.length === 0
        text: i18n.t("help.no_match")
        color: theme.muted
        font.pixelSize: Math.round(13 * root.s)
    }

    Grid {
        id: grid
        width: parent.width
        columns: root.availableWidth < Math.round(560 * root.s) ? 1 : 2
        columnSpacing: Math.round(40 * root.s)
        rowSpacing: Math.round(22 * root.s)

        readonly property real columnWidth: (width - columnSpacing * (columns - 1)) / columns

        Repeater {
            model: root.shownSections

            Column {
                required property var modelData

                width: grid.columnWidth
                spacing: Math.round(7 * root.s)

                Text {
                    bottomPadding: Math.round(2 * root.s)
                    text: modelData.title.toUpperCase()
                    color: theme.accent
                    font.pixelSize: Math.round(11 * root.s)
                    font.letterSpacing: 1
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: modelData.items

                    Row {
                        required property var modelData

                        width: parent.width
                        spacing: Math.round(14 * root.s)

                        Text {
                            id: keysText
                            width: Math.round(130 * root.s)
                            text: modelData[0]
                            color: theme.foreground
                            wrapMode: Text.WordWrap
                            font.family: systemTheme.monoFamily
                            font.pixelSize: Math.round(12 * root.s)
                        }

                        Text {
                            width: parent.width - keysText.width - parent.spacing
                            text: modelData[1]
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            font.pixelSize: Math.round(13 * root.s)
                        }
                    }
                }
            }
        }
    }
}
