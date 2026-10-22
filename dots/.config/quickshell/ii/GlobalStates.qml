import qs.modules.common
import qs.services
import QtQuick
import Quickshell
import Quickshell.Hyprland
import Quickshell.Io
pragma Singleton
pragma ComponentBehavior: Bound

Singleton {
    id: root
    property bool barOpen: true
    property bool cheatsheetOpen: false
    property bool crosshairOpen: false
    property bool displayPanelOpen: false
    property bool sidebarLeftOpen: false
    property bool sidebarRightOpen: false
    property bool mediaControlsOpen: false
    property bool osdBrightnessOpen: false
    property bool osdVolumeOpen: false
    // Transisi migrasi: dots-osd menonaktifkan OSD Quickshell sementara agar
    // tidak ada dua kartu OSD saat volume/brightness berubah.
    property bool nativeOsdActive: false
    property bool oskOpen: false
    property bool overlayOpen: false
    property bool overviewAppsVisible: true
    property bool overviewOpen: false
    property bool regionSelectorOpen: false
    // Region selector arguments. Kept here so the trigger (PanelStubs) can set
    // them before the panel itself is loaded.
    property int regionAction: 0
    property int regionSelectionMode: 0
    property bool searchOpen: false
    property bool screenLocked: false
    property bool screenLockContainsCharacters: false
    property bool screenUnlockFailed: false
    property bool screenTranslatorOpen: false
    property bool sessionOpen: false
    property bool superDown: false
    property bool superReleaseMightTrigger: true
    property bool wallpaperSelectorOpen: false
    property bool workspaceShowNumbers: false

    onSidebarRightOpenChanged: {
        if (GlobalStates.sidebarRightOpen) {
            GlobalStates.displayPanelOpen = false;
            Notifications.timeoutAll();
            Notifications.markAllRead();
        }
    }

    onDisplayPanelOpenChanged: {
        if (GlobalStates.displayPanelOpen) {
            GlobalStates.sidebarRightOpen = false;
        }
    }

    GlobalShortcut {
        name: "workspaceNumber"
        description: "Hold to show workspace numbers, release to show icons"

        onPressed: {
            root.superDown = true
        }
        onReleased: {
            root.superDown = false
        }
    }
}