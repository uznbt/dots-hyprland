import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import qs
import qs.services
import qs.modules.common

/**
 * Trigger layer for the lazy panels.
 *
 * The panels themselves are only loaded when they are used (see PanelHost),
 * so their IPC handlers and global shortcuts have to live somewhere that is
 * always loaded. Everything here only touches GlobalStates or a service; the
 * panels' windows react to those flags. No panel internals are referenced, so
 * a panel can stay unloaded until the first time it is actually opened.
 */
Scope {
    id: root

    // Values from modules/ii/regionSelector/RegionSelection.qml
    // (enum SnipAction / enum SelectionMode). Kept as numbers so this file
    // does not import the regionSelector module, which would compile
    // RegionSelection.qml at startup.
    readonly property int snipCopy: 0
    readonly property int snipSearch: 2
    readonly property int snipCharRecognition: 3
    readonly property int snipRecord: 4
    readonly property int snipRecordWithSound: 5
    readonly property int selectionRectCorners: 0
    readonly property int selectionCircle: 1

    function toggleWallpaperSelector() {
        if (Config.options.wallpaperSelector.useSystemFileDialog) {
            Wallpapers.openFallbackPicker(Appearance.m3colors.darkmode);
            return;
        }
        GlobalStates.wallpaperSelectorOpen = !GlobalStates.wallpaperSelectorOpen;
    }

    function regionOpen(action, mode) {
        GlobalStates.regionAction = action;
        GlobalStates.regionSelectionMode = mode;
        GlobalStates.regionSelectorOpen = true;
    }

    // ── Cheatsheet ──────────────────────────────────────────────────────────
    IpcHandler {
        target: "cheatsheet"
        function toggle(): void { GlobalStates.cheatsheetOpen = !GlobalStates.cheatsheetOpen }
        function open(): void { GlobalStates.cheatsheetOpen = true }
        function close(): void { GlobalStates.cheatsheetOpen = false }
    }
    GlobalShortcut {
        name: "cheatsheetToggle"
        description: "Toggles cheatsheet on press"
        onPressed: GlobalStates.cheatsheetOpen = !GlobalStates.cheatsheetOpen
    }
    GlobalShortcut {
        name: "cheatsheetOpen"
        description: "Opens cheatsheet on press"
        onPressed: GlobalStates.cheatsheetOpen = true
    }
    GlobalShortcut {
        name: "cheatsheetClose"
        description: "Closes cheatsheet on press"
        onPressed: GlobalStates.cheatsheetOpen = false
    }

    // ── Display panel ───────────────────────────────────────────────────────
    IpcHandler {
        target: "displayPanel"
        function toggle(): void { GlobalStates.displayPanelOpen = !GlobalStates.displayPanelOpen }
        function open(): void { GlobalStates.displayPanelOpen = true }
        function close(): void { GlobalStates.displayPanelOpen = false }
        function refresh(): void { DisplayService.refresh() }
        function mode(value: string): void { DisplayService.applyDisplayMode(value) }
        function workspace(workspaceId: string, monitorName: string): void {
            const id = parseInt(workspaceId);
            if (!Number.isNaN(id))
                DisplayService.showWorkspaceOnMonitor(id, monitorName);
        }
    }
    GlobalShortcut {
        name: "displayPanelToggle"
        description: "Toggles display settings panel"
        onPressed: GlobalStates.displayPanelOpen = !GlobalStates.displayPanelOpen
    }
    GlobalShortcut {
        name: "displayPanelOpen"
        description: "Opens display settings panel"
        onPressed: GlobalStates.displayPanelOpen = true
    }
    GlobalShortcut {
        name: "displayPanelClose"
        description: "Closes display settings panel"
        onPressed: GlobalStates.displayPanelOpen = false
    }

    // ── Media controls ──────────────────────────────────────────────────────
    IpcHandler {
        target: "mediaControls"
        function toggle(): void {
            GlobalStates.mediaControlsOpen = !GlobalStates.mediaControlsOpen;
            if (GlobalStates.mediaControlsOpen)
                Notifications.timeoutAll();
        }
        function open(): void {
            GlobalStates.mediaControlsOpen = true;
            Notifications.timeoutAll();
        }
        function close(): void { GlobalStates.mediaControlsOpen = false }
    }
    GlobalShortcut {
        name: "mediaControlsToggle"
        description: "Toggles media controls on press"
        onPressed: GlobalStates.mediaControlsOpen = !GlobalStates.mediaControlsOpen
    }
    GlobalShortcut {
        name: "mediaControlsOpen"
        description: "Opens media controls on press"
        onPressed: GlobalStates.mediaControlsOpen = true
    }
    GlobalShortcut {
        name: "mediaControlsClose"
        description: "Closes media controls on press"
        onPressed: GlobalStates.mediaControlsOpen = false
    }

    // ── On-screen keyboard ──────────────────────────────────────────────────
    IpcHandler {
        target: "osk"
        function toggle(): void { GlobalStates.oskOpen = !GlobalStates.oskOpen }
        function open(): void { GlobalStates.oskOpen = true }
        function close(): void { GlobalStates.oskOpen = false }
    }
    GlobalShortcut {
        name: "oskToggle"
        description: "Toggles on screen keyboard on press"
        onPressed: GlobalStates.oskOpen = !GlobalStates.oskOpen
    }
    GlobalShortcut {
        name: "oskOpen"
        description: "Opens on screen keyboard on press"
        onPressed: GlobalStates.oskOpen = true
    }
    GlobalShortcut {
        name: "oskClose"
        description: "Closes on screen keyboard on press"
        onPressed: GlobalStates.oskOpen = false
    }

    // ── Overlay ─────────────────────────────────────────────────────────────
    IpcHandler {
        target: "overlay"
        function toggle(): void { GlobalStates.overlayOpen = !GlobalStates.overlayOpen }
    }
    GlobalShortcut {
        name: "overlayToggle"
        description: "Toggles overlay on press"
        onPressed: GlobalStates.overlayOpen = !GlobalStates.overlayOpen
    }

    // ── Session screen ──────────────────────────────────────────────────────
    IpcHandler {
        target: "session"
        function toggle(): void { GlobalStates.sessionOpen = !GlobalStates.sessionOpen }
        function open(): void { GlobalStates.sessionOpen = true }
        function close(): void { GlobalStates.sessionOpen = false }
    }
    GlobalShortcut {
        name: "sessionToggle"
        description: "Toggles session screen on press"
        onPressed: GlobalStates.sessionOpen = !GlobalStates.sessionOpen
    }
    GlobalShortcut {
        name: "sessionOpen"
        description: "Opens session screen on press"
        onPressed: GlobalStates.sessionOpen = true
    }
    GlobalShortcut {
        name: "sessionClose"
        description: "Closes session screen on press"
        onPressed: GlobalStates.sessionOpen = false
    }

    // ── Wallpaper selector ──────────────────────────────────────────────────
    IpcHandler {
        target: "wallpaperSelector"
        function toggle(): void { root.toggleWallpaperSelector() }
        function random(): void { Wallpapers.randomFromCurrentFolder() }
    }
    GlobalShortcut {
        name: "wallpaperSelectorToggle"
        description: "Toggle wallpaper selector"
        onPressed: root.toggleWallpaperSelector()
    }
    GlobalShortcut {
        name: "wallpaperSelectorRandom"
        description: "Select random wallpaper in current folder"
        onPressed: Wallpapers.randomFromCurrentFolder()
    }

    // ── Screen translator ───────────────────────────────────────────────────
    IpcHandler {
        target: "screenTranslator"
        function translate() { GlobalStates.screenTranslatorOpen = true }
    }
    GlobalShortcut {
        name: "screenTranslate"
        description: "Translates screen content"
        onPressed: GlobalStates.screenTranslatorOpen = true
    }

    // ── Region selector ─────────────────────────────────────────────────────
    IpcHandler {
        target: "region"
        function screenshot() { root.regionOpen(root.snipCopy, root.selectionRectCorners) }
        function search() {
            root.regionOpen(
                root.snipSearch,
                Config.options.search.imageSearch.useCircleSelection
                    ? root.selectionCircle
                    : root.selectionRectCorners
            )
        }
        function ocr() { root.regionOpen(root.snipCharRecognition, root.selectionRectCorners) }
        function record() { root.regionOpen(root.snipRecord, root.selectionRectCorners) }
        function recordWithSound() { root.regionOpen(root.snipRecordWithSound, root.selectionRectCorners) }
    }
    GlobalShortcut {
        name: "regionScreenshot"
        description: "Takes a screenshot of the selected region"
        onPressed: root.regionOpen(root.snipCopy, root.selectionRectCorners)
    }
    GlobalShortcut {
        name: "regionSearch"
        description: "Searches the selected region"
        onPressed: root.regionOpen(
            root.snipSearch,
            Config.options.search.imageSearch.useCircleSelection
                ? root.selectionCircle
                : root.selectionRectCorners
        )
    }
    GlobalShortcut {
        name: "regionOcr"
        description: "Recognizes text in the selected region"
        onPressed: root.regionOpen(root.snipCharRecognition, root.selectionRectCorners)
    }
    GlobalShortcut {
        name: "regionRecord"
        description: "Records the selected region"
        onPressed: root.regionOpen(root.snipRecord, root.selectionRectCorners)
    }
    GlobalShortcut {
        name: "regionRecordWithSound"
        description: "Records the selected region with sound"
        onPressed: root.regionOpen(root.snipRecordWithSound, root.selectionRectCorners)
    }
}
