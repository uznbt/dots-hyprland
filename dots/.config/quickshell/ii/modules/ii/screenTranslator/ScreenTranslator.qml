pragma ComponentBehavior: Bound
import qs
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland

Scope {
    id: root

    function dismiss() {
        GlobalStates.screenTranslatorOpen = false
    }

    readonly property var currentScreen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name) ?? null
    
    Loader {
        id: translatorLoader
        property var lockedScreen
        active: GlobalStates.screenTranslatorOpen
        Component.onCompleted: {
            if (translatorLoader.active)
                translatorLoader.lockedScreen = root.currentScreen;
        }
        onActiveChanged: {
            if (translatorLoader.active)
                translatorLoader.lockedScreen = root.currentScreen;
        }

        sourceComponent: ScreenTranslatorPanel {
            screen: translatorLoader.lockedScreen
            onDismiss: root.dismiss()
        }
    }
}
