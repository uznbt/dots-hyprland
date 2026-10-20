pragma ComponentBehavior: Bound

import qs
import qs.services
import qs.modules.common
import qs.modules.common.functions
import qs.modules.common.widgets
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Widgets

Rectangle {
    id: root

    readonly property var pinnedEntries: {
        const pinnedIds = Config.options?.launcher?.pinnedApps ?? [];
        return pinnedIds.map(id => DesktopEntries.byId(id)).filter(entry => entry);
    }
    readonly property var apps: {
        const pinned = root.pinnedEntries;
        const pinnedIds = pinned.map(entry => entry.id);
        const rest = AppSearch.list
            .filter(entry => !pinnedIds.includes(entry.id) && !entry.noDisplay)
            .sort((a, b) => a.name.localeCompare(b.name));
        return pinned.concat(rest);
    }

    property int columns: 10
    property int visibleRows: 3
    property real cellHeight: 74
    property real padding: 12

    property var contextEntry: null
    property bool contextPinnedTaskbar: false
    property bool contextPinnedFavorites: false

    implicitHeight: contentColumn.implicitHeight + root.padding * 2
    radius: Appearance.rounding.large
    color: Appearance.colors.colBackgroundSurfaceContainer

    StyledRectangularShadow {
        target: root
    }

    function launch(entry) {
        if (!entry)
            return;

        GlobalStates.overviewOpen = false;
        if (!entry.runInTerminal) {
            entry.execute();
            return;
        }
        Quickshell.execDetached([
            "bash", "-c",
            `${Config.options.apps.terminal} -e '${StringUtils.shellSingleQuoteEscape(entry.command.join(" "))}'`
        ]);
    }

    function openContextMenu(entry, sourceItem, event) {
        if (!entry)
            return;

        contextEntry = entry;
        contextPinnedTaskbar = TaskbarApps.isPinned(entry.id);
        contextPinnedFavorites = LauncherApps.isPinned(entry.id);

        if (sourceItem && event) {
            const point = sourceItem.mapToItem(root, event.x, event.y);
            const menuHeight = contextMenu.implicitHeight || 160;
            contextMenu.x = Math.max(4, Math.min(point.x + 6, root.width - contextMenu.width - 4));
            contextMenu.y = Math.max(4, Math.min(point.y + 6, root.height - menuHeight - 4));
        } else {
            contextMenu.x = 8;
            contextMenu.y = 8;
        }

        contextMenu.open();
    }

    ColumnLayout {
        id: contentColumn

        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: root.padding
        }
        spacing: 8

        GridView {
            id: appGrid

            Layout.fillWidth: true
            Layout.preferredHeight: root.visibleRows * root.cellHeight
            cellWidth: width / root.columns
            cellHeight: root.cellHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.apps
            focus: false

            ScrollBar.vertical: StyledScrollBar {}

            delegate: RippleButton {
                id: appTile

                required property var modelData

                width: appGrid.cellWidth
                height: appGrid.cellHeight
                padding: 3
                buttonRadius: Appearance.rounding.normal
                colBackground: "transparent"
                colBackgroundHover: Appearance.colors.colLayer1Hover
                colRipple: Appearance.colors.colLayer1Active
                onClicked: root.launch(appTile.modelData)
                altAction: event => root.openContextMenu(appTile.modelData, appTile, event)

                contentItem: ColumnLayout {
                    spacing: 2

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32

                        IconImage {
                            anchors.centerIn: parent
                            source: Quickshell.iconPath(appTile.modelData.icon, "application-x-executable")
                            implicitSize: 30
                        }
                    }

                    StyledText {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: appTile.modelData.name
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colOnLayer1
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignTop
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Menu {
        id: contextMenu

        width: 230
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: Appearance.rounding.normal
            color: Appearance.colors.colLayer0Base
            border.width: 1
            border.color: Appearance.colors.colOutlineVariant
        }

        MenuItem {
            id: openItem

            width: contextMenu.width - 8
            implicitHeight: 36
            leftPadding: 12
            rightPadding: 12
            background: Rectangle {
                radius: Appearance.rounding.small
                color: openItem.hovered ? Appearance.colors.colLayer1Hover : "transparent"
            }
            contentItem: RowLayout {
                spacing: 10

                MaterialSymbol {
                    Layout.alignment: Qt.AlignVCenter
                    text: "open_in_new"
                    iconSize: 18
                    color: Appearance.colors.colOnLayer1
                }

                StyledText {
                    Layout.fillWidth: true
                    text: Translation.tr("Buka")
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.colors.colOnLayer1
                }
            }
            onTriggered: root.launch(root.contextEntry)
        }

        MenuSeparator {
            contentItem: Item {
                implicitHeight: 9

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: 1
                    color: Appearance.colors.colOutlineVariant
                }
            }
        }

        MenuItem {
            id: taskbarItem

            width: contextMenu.width - 8
            implicitHeight: 36
            leftPadding: 12
            rightPadding: 12
            background: Rectangle {
                radius: Appearance.rounding.small
                color: taskbarItem.hovered ? Appearance.colors.colLayer1Hover : "transparent"
            }
            contentItem: RowLayout {
                spacing: 10

                MaterialSymbol {
                    Layout.alignment: Qt.AlignVCenter
                    text: root.contextPinnedTaskbar ? "keep_off" : "keep"
                    iconSize: 18
                    color: Appearance.colors.colOnLayer1
                }

                StyledText {
                    Layout.fillWidth: true
                    text: root.contextPinnedTaskbar
                        ? Translation.tr("Lepas dari taskbar")
                        : Translation.tr("Sematkan ke taskbar")
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.colors.colOnLayer1
                }
            }
            onTriggered: {
                if (root.contextEntry)
                    TaskbarApps.togglePin(root.contextEntry.id);
            }
        }

        MenuItem {
            id: favoriteItem

            width: contextMenu.width - 8
            implicitHeight: 36
            leftPadding: 12
            rightPadding: 12
            background: Rectangle {
                radius: Appearance.rounding.small
                color: favoriteItem.hovered ? Appearance.colors.colLayer1Hover : "transparent"
            }
            contentItem: RowLayout {
                spacing: 10

                MaterialSymbol {
                    Layout.alignment: Qt.AlignVCenter
                    text: root.contextPinnedFavorites ? "star" : "star_border"
                    iconSize: 18
                    color: Appearance.colors.colOnLayer1
                }

                StyledText {
                    Layout.fillWidth: true
                    text: root.contextPinnedFavorites
                        ? Translation.tr("Lepas dari favorit")
                        : Translation.tr("Sematkan ke favorit")
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.colors.colOnLayer1
                }
            }
            onTriggered: {
                if (root.contextEntry)
                    LauncherApps.togglePin(root.contextEntry.id);
            }
        }
    }
}
