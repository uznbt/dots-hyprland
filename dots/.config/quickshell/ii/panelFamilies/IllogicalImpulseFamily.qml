import QtQuick
import Quickshell

import qs
import qs.modules.common
import qs.modules.ii.background
import qs.modules.ii.bar
import qs.modules.ii.cheatsheet
import qs.modules.ii.displayPanel
import qs.modules.ii.dock
import qs.modules.ii.lock
import qs.modules.ii.mediaControls
import qs.modules.ii.notificationPopup
import qs.modules.ii.onScreenDisplay
import qs.modules.ii.onScreenKeyboard
import qs.modules.ii.overview
import qs.modules.ii.polkit
import qs.modules.ii.regionSelector
import qs.modules.ii.screenCorners
import qs.modules.ii.screenTranslator
import qs.modules.ii.sessionScreen
import qs.modules.ii.sidebarLeft
import qs.modules.ii.sidebarRight
import qs.modules.ii.timerIndicator
import qs.modules.ii.overlay
import qs.modules.ii.verticalBar
import qs.modules.ii.wallpaperSelector

Scope {
    // ── Permanent UI ────────────────────────────────────────────────────────
    PanelLoader { extraCondition: !Config.options.bar.vertical; component: Bar {} }
    PanelLoader { component: Background {} }
    PanelLoader { extraCondition: Config.options.dock.enable; component: Dock {} }
    PanelLoader { component: Lock {} }
    PanelLoader { component: NotificationPopup {} }
    PanelLoader { component: Polkit {} }
    PanelLoader { component: SidebarLeft {} }
    PanelLoader { component: SidebarRight {} }
    PanelLoader { component: Overview {} }
    PanelLoader { component: OnScreenDisplay {} }
    PanelLoader { component: ScreenCorners {} }
    PanelLoader { component: TimerIndicator {} }
    PanelLoader { extraCondition: Config.options.bar.vertical; component: VerticalBar {} }

    // ── Created on first use ────────────────────────────────────────────────
    // Triggers (IPC + global shortcuts) live in PanelStubs, which is always
    // loaded and only sets these GlobalStates flags.
    PanelLoader { lazy: true; open: GlobalStates.cheatsheetOpen; component: Cheatsheet {} }
    PanelLoader { lazy: true; open: GlobalStates.displayPanelOpen; component: DisplayPanel {} }
    PanelLoader { lazy: true; open: GlobalStates.mediaControlsOpen; component: MediaControls {} }
    PanelLoader { lazy: true; open: GlobalStates.oskOpen; component: OnScreenKeyboard {} }
    PanelLoader { lazy: true; open: GlobalStates.overlayOpen; component: Overlay {} }
    PanelLoader { lazy: true; open: GlobalStates.regionSelectorOpen; component: RegionSelector {} }
    PanelLoader { lazy: true; open: GlobalStates.screenTranslatorOpen; component: ScreenTranslator {} }
    PanelLoader { lazy: true; open: GlobalStates.sessionOpen; component: SessionScreen {} }
    PanelLoader { lazy: true; open: GlobalStates.wallpaperSelectorOpen; component: WallpaperSelector {} }

    // IPC handlers + global shortcuts for the panels above.
    PanelStubs {}
}
