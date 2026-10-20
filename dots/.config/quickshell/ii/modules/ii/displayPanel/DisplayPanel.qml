import qs
import qs.services
import qs.modules.common
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland

Scope {
    id: root

    property int panelWidth: Appearance.sizes.sidebarWidth

    PanelWindow {
        id: panelWindow
        visible: GlobalStates.displayPanelOpen

        function hide() {
            GlobalStates.displayPanelOpen = false;
        }

        exclusiveZone: 0
        implicitWidth: panelWidth
        WlrLayershell.namespace: "quickshell:displayPanel"
        WlrLayershell.keyboardFocus: GlobalStates.displayPanelOpen
            ? WlrKeyboardFocus.OnDemand
            : WlrKeyboardFocus.None
        color: "transparent"

        anchors {
            top: true
            right: true
            bottom: true
        }

        onVisibleChanged: {
            if (visible) {
                GlobalStates.sidebarRightOpen = false;
                DisplayService.refresh();
                GlobalFocusGrab.addDismissable(panelWindow);
            } else {
                GlobalFocusGrab.removeDismissable(panelWindow);
            }
        }

        Connections {
            target: GlobalFocusGrab
            function onDismissed() {
                panelWindow.hide();
            }
        }

        Loader {
            id: displayContentLoader
            active: GlobalStates.displayPanelOpen
            anchors {
                fill: parent
                margins: Appearance.sizes.hyprlandGapsOut
                leftMargin: Appearance.sizes.elevationMargin
            }
            width: panelWidth - Appearance.sizes.hyprlandGapsOut - Appearance.sizes.elevationMargin
            height: parent.height - Appearance.sizes.hyprlandGapsOut * 2

            focus: GlobalStates.displayPanelOpen
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    panelWindow.hide();
                }
            }

            sourceComponent: DisplayPanelContent {}
        }
    }

    IpcHandler {
        target: "displayPanel"

        function toggle(): void {
            GlobalStates.displayPanelOpen = !GlobalStates.displayPanelOpen;
        }

        function open(): void {
            GlobalStates.displayPanelOpen = true;
        }

        function close(): void {
            GlobalStates.displayPanelOpen = false;
        }

        function refresh(): void {
            DisplayService.refresh();
        }

        function mode(value: string): void {
            DisplayService.applyDisplayMode(value);
        }

        function workspace(workspaceId: string, monitorName: string): void {
            const id = parseInt(workspaceId);
            if (!Number.isNaN(id))
                DisplayService.showWorkspaceOnMonitor(id, monitorName);
        }
    }

    GlobalShortcut {
        name: "displayPanelToggle"
        description: "Toggles display settings panel"

        onPressed: {
            GlobalStates.displayPanelOpen = !GlobalStates.displayPanelOpen;
        }
    }

    GlobalShortcut {
        name: "displayPanelOpen"
        description: "Opens display settings panel"

        onPressed: {
            GlobalStates.displayPanelOpen = true;
        }
    }

    GlobalShortcut {
        name: "displayPanelClose"
        description: "Closes display settings panel"

        onPressed: {
            GlobalStates.displayPanelOpen = false;
        }
    }
}
