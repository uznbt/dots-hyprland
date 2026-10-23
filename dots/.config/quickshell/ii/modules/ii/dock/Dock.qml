import qs
import qs.services
import qs.modules.common
import qs.modules.common.widgets
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import Quickshell.Io
import Quickshell
import Quickshell.Widgets
import Quickshell.Wayland
import Quickshell.Hyprland

Scope { // Scope
    id: root
    property bool pinned: Config.options?.dock.pinnedOnStartup ?? false
    property bool contextMenuOpen: false
    property string contextMenuMode: "dock"
    property string contextMenuAppId: ""
    property var contextMenuScreen: null
    readonly property bool contextMenuAppPinned: contextMenuAppId !== "" && TaskbarApps.isPinned(contextMenuAppId)
    readonly property int contextMenuAppIndex: {
        if (contextMenuAppId === "") return -1;
        const pinned = Config.options?.dock.pinnedApps ?? [];
        return pinned.indexOf(contextMenuAppId);
    }

    function openDockContextMenu(mode, appId, screen) {
        contextMenuMode = mode;
        contextMenuAppId = appId ?? "";
        contextMenuScreen = screen ?? null;
        contextMenuOpenTimer.restart();
    }

    function closeDockContextMenu() {
        contextMenuOpenTimer.stop();
        contextMenuOpen = false;
    }

    Timer {
        id: contextMenuOpenTimer
        interval: 80
        onTriggered: root.contextMenuOpen = true
    }

    function movePinnedApp(delta) {
        const pinned = (Config.options?.dock.pinnedApps ?? []).slice();
        const index = pinned.indexOf(contextMenuAppId);
        const target = index + delta;
        if (index < 0 || target < 0 || target >= pinned.length) return;
        const tmp = pinned[index];
        pinned[index] = pinned[target];
        pinned[target] = tmp;
        Config.options.dock.pinnedApps = pinned;
    }

    component DockContextMenuAction: RippleButton {
        id: menuAction
        property string iconName
        property string label
        property var actionCallback

        Layout.fillWidth: true
        implicitHeight: 36
        buttonRadius: Appearance.rounding.small
        colBackground: "transparent"
        colBackgroundHover: Appearance.colors.colLayer1Hover
        contentItem: RowLayout {
            spacing: 10
            MaterialSymbol {
                Layout.alignment: Qt.AlignVCenter
                text: menuAction.iconName
                iconSize: 18
                color: Appearance.colors.colOnLayer1
            }
            StyledText {
                Layout.fillWidth: true
                text: menuAction.label
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.colors.colOnLayer1
            }
        }
        onClicked: {
            if (menuAction.actionCallback) menuAction.actionCallback();
        }
    }

    Variants {
        // For each monitor
        model: Quickshell.screens

        PanelWindow {
            id: dockRoot
            // Window
            required property var modelData
            screen: modelData
            visible: !GlobalStates.screenLocked

            property bool reveal: root.pinned || (Config.options?.dock.hoverToReveal && dockMouseArea.containsMouse) || dockApps.requestDockShow || (!ToplevelManager.activeToplevel?.activated)

            anchors {
                bottom: true
                left: true
                right: true
            }

            exclusiveZone: root.pinned ? implicitHeight - (Appearance.sizes.hyprlandGapsOut) - (Appearance.sizes.elevationMargin - Appearance.sizes.hyprlandGapsOut) : 0

            implicitWidth: dockBackground.implicitWidth
            WlrLayershell.namespace: "quickshell:dock"
            color: "transparent"

            implicitHeight: (Config.options?.dock.height ?? 70) + Appearance.sizes.elevationMargin + Appearance.sizes.hyprlandGapsOut


            mask: Region {
                item: dockMouseArea
            }

            MouseArea {
                id: dockMouseArea
                height: parent.height
                anchors {
                    top: parent.top
                    topMargin: dockRoot.reveal ? 0 : Config.options?.dock.hoverToReveal ? (dockRoot.implicitHeight - Config.options.dock.hoverRegionHeight) : (dockRoot.implicitHeight + 1)
                    horizontalCenter: parent.horizontalCenter
                }
                implicitWidth: dockHoverRegion.implicitWidth + Appearance.sizes.elevationMargin * 2
                hoverEnabled: true

                Behavior on anchors.topMargin {
                    animation: Appearance.animation.elementMoveFast.numberAnimation.createObject(this)
                }

                Item {
                    id: dockHoverRegion
                    anchors.fill: parent
                    implicitWidth: dockBackground.implicitWidth

                    Item { // Wrapper for the dock background
                        id: dockBackground
                        anchors {
                            top: parent.top
                            bottom: parent.bottom
                            horizontalCenter: parent.horizontalCenter
                        }

                        implicitWidth: dockRow.implicitWidth + 5 * 2
                        height: parent.height - Appearance.sizes.elevationMargin - Appearance.sizes.hyprlandGapsOut

                        StyledRectangularShadow {
                            target: dockVisualBackground
                        }
                        Rectangle { // The real rectangle that is visible
                            id: dockVisualBackground
                            property real margin: Appearance.sizes.elevationMargin
                            anchors.fill: parent
                            anchors.topMargin: Appearance.sizes.elevationMargin
                            anchors.bottomMargin: Appearance.sizes.hyprlandGapsOut
                            color: Appearance.colors.colLayer0
                            border.width: 1
                            border.color: Appearance.colors.colLayer0Border
                            radius: Appearance.rounding.large
                        }

                        MouseArea {
                            id: dockContextArea
                            anchors.fill: dockVisualBackground
                            acceptedButtons: Qt.RightButton
                            onPressed: event => {
                                if (event.button === Qt.RightButton) {
                                    root.openDockContextMenu("dock", "", dockRoot.modelData);
                                    event.accepted = true;
                                }
                            }
                        }

                        RowLayout {
                            id: dockRow
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            spacing: 3
                            property real padding: 5

                            DockApps {
                                id: dockApps
                                buttonPadding: dockRow.padding
                                menuScreen: dockRoot.modelData
                                contextMenuOpen: root.contextMenuOpen
                                onAppContextMenuRequested: function(appId, screen) {
                                    root.openDockContextMenu("app", appId, screen);
                                }
                            }
                            DockSeparator {}
                            DockButton {
                                Layout.fillHeight: true
                                onClicked: GlobalStates.overviewOpen = !GlobalStates.overviewOpen
                                topInset: Appearance.sizes.hyprlandGapsOut + dockRow.padding
                                bottomInset: Appearance.sizes.hyprlandGapsOut + dockRow.padding
                                contentItem: MaterialSymbol {
                                    anchors.fill: parent
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: parent.width / 2
                                    text: "apps"
                                    color: Appearance.colors.colOnLayer0
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    PanelWindow {
        id: contextMenuWindow
        screen: root.contextMenuScreen ?? Quickshell.screens.find(s => true)
        visible: root.contextMenuOpen
        color: "transparent"
        anchors {
            top: true
            bottom: true
            left: true
            right: true
        }
        exclusiveZone: -1
        WlrLayershell.namespace: "quickshell:dock-menu"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: root.contextMenuOpen ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None
        property bool backdropReady: false

        onVisibleChanged: {
            if (visible) {
                backdropReady = false;
                backdropReadyTimer.restart();
            } else {
                backdropReadyTimer.stop();
            }
        }

        Timer {
            id: backdropReadyTimer
            interval: 250
            onTriggered: contextMenuWindow.backdropReady = true
        }

        MouseArea {
            id: contextMenuBackdrop
            anchors.fill: parent
            enabled: contextMenuWindow.backdropReady
            acceptedButtons: Qt.AllButtons
            focus: root.contextMenuOpen
            Keys.onEscapePressed: root.closeDockContextMenu()
            onClicked: root.closeDockContextMenu()
        }

        Rectangle {
            id: contextMenuPanel
            width: 260
            height: contextMenuColumn.implicitHeight + 8
            x: (parent.width - width) / 2
            y: parent.height - 92 - height
            color: Appearance.colors.colLayer1Base
            border.width: 1
            border.color: Appearance.colors.colOutlineVariant
            radius: Appearance.rounding.normal
            focus: root.contextMenuOpen
            Keys.onEscapePressed: root.closeDockContextMenu()

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onClicked: {}
            }

            ColumnLayout {
                id: contextMenuColumn
                anchors.fill: parent
                anchors.margins: 4
                spacing: 2

                DockContextMenuAction {
                    visible: root.contextMenuMode === "dock"
                    iconName: root.pinned ? "keep_off" : "keep"
                    label: root.pinned ? Translation.tr("Lepas pin dock") : Translation.tr("Sematkan dock")
                    actionCallback: () => {
                        root.closeDockContextMenu();
                        Config.options.dock.pinnedOnStartup = !Config.options.dock.pinnedOnStartup;
                    }
                }

                DockContextMenuAction {
                    visible: root.contextMenuMode === "dock"
                    iconName: "settings"
                    label: Translation.tr("Pengaturan dock")
                    actionCallback: () => {
                        root.closeDockContextMenu();
                        Quickshell.execDetached(["qs", "-c", "ii", "ipc", "call", "settings", "open"]);
                    }
                }

                DockContextMenuAction {
                    visible: root.contextMenuMode === "app"
                    iconName: root.contextMenuAppPinned ? "keep_off" : "keep"
                    label: root.contextMenuAppPinned ? Translation.tr("Lepas dari dock") : Translation.tr("Sematkan ke dock")
                    actionCallback: () => {
                        const appId = root.contextMenuAppId;
                        root.closeDockContextMenu();
                        TaskbarApps.togglePin(appId);
                    }
                }

                DockContextMenuAction {
                    visible: root.contextMenuMode === "app" && root.contextMenuAppPinned && root.contextMenuAppIndex > 0
                    iconName: "arrow_back"
                    label: Translation.tr("Geser ke kiri")
                    actionCallback: () => {
                        root.movePinnedApp(-1);
                        root.closeDockContextMenu();
                    }
                }

                DockContextMenuAction {
                    visible: root.contextMenuMode === "app" && root.contextMenuAppPinned
                        && root.contextMenuAppIndex >= 0
                        && root.contextMenuAppIndex < (Config.options?.dock.pinnedApps.length ?? 0) - 1
                    iconName: "arrow_forward"
                    label: Translation.tr("Geser ke kanan")
                    actionCallback: () => {
                        root.movePinnedApp(1);
                        root.closeDockContextMenu();
                    }
                }
            }
        }
    }
}
