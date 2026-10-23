import qs
import qs.services
import qs.modules.common
import qs.modules.common.widgets
import qs.modules.common.functions
import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Wayland

/**
 * Compact wallpaper carousel inspired by Caelestia's launcher picker.
 * The selected wallpaper is centered and enlarged; neighbours are smaller.
 * Arrow keys move the selection, Enter applies it, Escape closes the strip.
 */
Scope {
    id: root

    Loader {
        id: stripLoader
        active: GlobalStates.wallpaperStripOpen

        sourceComponent: PanelWindow {
            id: panelWindow
            visible: true

            exclusionMode: ExclusionMode.Ignore
            exclusiveZone: 0
            implicitWidth: card.width + Appearance.sizes.elevationMargin * 2
            implicitHeight: card.height + Appearance.sizes.elevationMargin * 2
            color: "transparent"
            WlrLayershell.namespace: "quickshell:wallpaperStrip"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

            anchors {
                top: true
                bottom: true
                left: true
                right: true
            }

            mask: Region {
                item: card
            }

            Component.onCompleted: {
                GlobalFocusGrab.addDismissable(panelWindow);
            }
            Component.onDestruction: {
                GlobalFocusGrab.removeDismissable(panelWindow);
            }

            Connections {
                target: GlobalFocusGrab
                function onDismissed() {
                    GlobalStates.wallpaperStripOpen = false;
                }
            }

            StyledRectangularShadow {
                target: card
            }

            Rectangle {
                id: card
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Config?.options.bar.vertical
                    ? Appearance.sizes.hyprlandGapsOut
                    : Appearance.sizes.barHeight + Appearance.rounding.screenRounding + Appearance.sizes.hyprlandGapsOut
                width: Math.min((panelWindow.screen ? panelWindow.screen.width : 1280) - 48, 940)
                height: 302
                radius: Appearance.rounding.windowRounding
                color: Appearance.colors.colLayer0Base
                border.width: 1
                border.color: Appearance.colors.colLayer0Border
                clip: true
                focus: true

                property var items: Wallpapers.wallpaperItems
                property int currentIndex: 0

                function syncSelection() {
                    const count = rootItems().length;
                    if (count === 0) {
                        currentIndex = 0;
                        return;
                    }

                    const currentPath = FileUtils.trimFileProtocol(Config.options?.background?.wallpaperPath ?? "");
                    let index = -1;
                    if (currentPath.length > 0) {
                        index = rootItems().findIndex(item => item.path === currentPath);
                    }
                    if (index < 0) index = Math.min(currentIndex, count - 1);
                    currentIndex = index;
                    pathView.currentIndex = index;
                }

                function rootItems() {
                    return Wallpapers.wallpaperItems ?? [];
                }

                function moveSelection(delta) {
                    const count = rootItems().length;
                    if (count === 0) return;
                    pathView.currentIndex = Math.max(0, Math.min(count - 1, pathView.currentIndex + delta));
                }

                function applyCurrent() {
                    const item = rootItems()[pathView.currentIndex];
                    if (!item) return;
                    Wallpapers.apply(item.path, Appearance.m3colors.darkmode);
                    GlobalStates.wallpaperStripOpen = false;
                }

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Escape) {
                        GlobalStates.wallpaperStripOpen = false;
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Up) {
                        moveSelection(-1);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Down) {
                        moveSelection(1);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        applyCurrent();
                        event.accepted = true;
                    }
                }

                Component.onCompleted: {
                    Wallpapers.prepareForStrip();
                    syncSelection();
                    Qt.callLater(() => {
                        card.forceActiveFocus();
                        pathView.currentIndex = card.currentIndex;
                    });
                }

                Connections {
                    target: Wallpapers
                    function onWallpaperItemsChanged() {
                        card.syncSelection();
                    }
                }

                ColumnLayout {
                    anchors {
                        fill: parent
                        margins: 18
                    }
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        StyledText {
                            text: "Wallpaper"
                            color: Appearance.colors.colOnLayer0
                            font {
                                pixelSize: Appearance.font.pixelSize.normal
                                weight: Font.Medium
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        StyledText {
                            text: `${card.items.length > 0 ? card.currentIndex + 1 : 0} / ${card.items.length}`
                            color: Appearance.colors.colOnLayer0
                            opacity: 0.65
                            font.pixelSize: Appearance.font.pixelSize.smaller
                        }
                    }

                    PathView {
                        id: pathView
                        Layout.fillWidth: true
                        Layout.preferredHeight: 218
                        focus: false
                        interactive: true
                        model: card.items
                        pathItemCount: 3
                        cacheItemCount: 4
                        snapMode: PathView.SnapToItem
                        preferredHighlightBegin: 0.5
                        preferredHighlightEnd: 0.5
                        highlightRangeMode: PathView.StrictlyEnforceRange
                        currentIndex: card.currentIndex
                        onCurrentIndexChanged: {
                            if (card.currentIndex !== currentIndex)
                                card.currentIndex = currentIndex;
                        }

                        path: Path {
                            startY: pathView.height / 2

                            PathLine {
                                x: pathView.width / 2
                                relativeY: 0
                            }
                            PathLine {
                                x: pathView.width
                                relativeY: 0
                            }
                        }

                        delegate: Item {
                            id: wallpaperItem
                            required property var modelData
                            required property int index

                            readonly property real itemWidth: Math.min(300, pathView.width / 3)
                            width: itemWidth
                            height: 218
                            scale: PathView.isCurrentItem ? 1.0 : 0.8
                            opacity: PathView.onPath ? 1.0 : 0.0
                            z: PathView.isCurrentItem ? 3 : 1

                            Behavior on scale {
                                NumberAnimation {
                                    duration: 220
                                    easing.type: Easing.OutCubic
                                }
                            }

                            Behavior on opacity {
                                NumberAnimation {
                                    duration: 160
                                }
                            }

                            StyledRectangularShadow {
                                target: imageFrame
                                opacity: PathView.isCurrentItem ? 1 : 0

                                Behavior on opacity {
                                    NumberAnimation {
                                        duration: 180
                                    }
                                }
                            }

                            Rectangle {
                                id: imageFrame
                                width: wallpaperItem.itemWidth
                                height: width * 9 / 16
                                anchors.horizontalCenter: parent.horizontalCenter
                                radius: Appearance.rounding.normal
                                color: Appearance.colors.colLayer1

                                layer.enabled: true
                                layer.effect: OpacityMask {
                                    maskSource: Rectangle {
                                        width: imageFrame.width
                                        height: imageFrame.height
                                        radius: imageFrame.radius
                                    }
                                }

                                MaterialSymbol {
                                    anchors.centerIn: parent
                                    text: "image"
                                    iconSize: 38
                                    color: Appearance.colors.colOnLayer1
                                    opacity: 0.35
                                }

                                StyledImage {
                                    anchors.fill: parent
                                    source: wallpaperItem.modelData.path
                                    fillMode: Image.PreserveAspectCrop
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: imageFrame.radius
                                    color: Appearance.colors.colOnLayer0
                                    opacity: itemMouseArea.containsMouse && !PathView.isCurrentItem ? 0.06 : 0

                                    Behavior on opacity {
                                        NumberAnimation {
                                            duration: 120
                                        }
                                    }
                                }
                            }

                            StyledText {
                                id: itemLabel
                                anchors {
                                    top: imageFrame.bottom
                                    topMargin: 7
                                    horizontalCenter: parent.horizontalCenter
                                }
                                width: imageFrame.width
                                text: wallpaperItem.modelData.label
                                color: PathView.isCurrentItem
                                    ? Appearance.colors.colOnLayer0
                                    : Appearance.colors.colOnLayer1
                                opacity: PathView.isCurrentItem ? 1 : 0.65
                                elide: Text.ElideMiddle
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: Appearance.font.pixelSize.smallie
                            }

                            MouseArea {
                                id: itemMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    pathView.currentIndex = wallpaperItem.index;
                                    card.forceActiveFocus();
                                }
                                onDoubleClicked: {
                                    pathView.currentIndex = wallpaperItem.index;
                                    card.applyCurrent();
                                }
                            }
                        }
                    }

                    StyledText {
                        Layout.fillWidth: true
                        text: Wallpapers.wallpaperItems.length > 0
                            ? "← → pilih  •  Enter pasang  •  Esc batal"
                            : "Tidak ada wallpaper di folder ini  •  Esc untuk menutup"
                        color: Appearance.colors.colOnLayer0
                        opacity: 0.55
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Appearance.font.pixelSize.smaller
                    }
                }
            }
        }
    }
}
