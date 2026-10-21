//@ pragma UseQApplication
//@ pragma Env QS_NO_RELOAD_POPUP=1
//@ pragma Env QT_QUICK_CONTROLS_STYLE=Basic
//@ pragma Env QT_QUICK_FLICKABLE_WHEEL_DECELERATION=10000

// Remove two slashes below and adjust the value to change the UI scale
////@ pragma Env QT_SCALE_FACTOR=1

import "modules/common"
import "services"
import "panelFamilies"

import QtQuick
import QtQuick.Window
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland

ShellRoot {
    id: root

    // Stuff for every panel family
    ReloadPopup {}

    Component.onCompleted: {
        MaterialThemeLoader.reapplyTheme()
        Hyprsunset.load()
        FirstRunExperience.load()
        ConflictKiller.load()
        Cliphist.refresh()
        Wallpapers.load()
        Updates.load()
    }


    // Panel families
    property list<string> families: ["ii", "waffle"]
    function cyclePanelFamily() {
        const currentIndex = families.indexOf(Config.options.panelFamily)
        const nextIndex = (currentIndex + 1) % families.length
        Config.options.panelFamily = families[nextIndex]
    }

    component PanelFamilyLoader: LazyLoader {
        required property string identifier
        property bool extraCondition: true
        active: Config.ready && Config.options.panelFamily === identifier && extraCondition
    }
    
    // Pakai `source` (bukan `component`) supaya family yang tidak dipakai
    // tidak ikut dikompilasi saat shell start. Family "waffle" berisi ~140
    // file QML yang tidak perlu dimuat kalau kita memakai "ii".
    PanelFamilyLoader {
        identifier: "ii"
        source: "panelFamilies/IllogicalImpulseFamily.qml"
    }

    PanelFamilyLoader {
        identifier: "waffle"
        source: "panelFamilies/WaffleFamily.qml"
    }


    // Settings window.
    // The settings app used to be a separate `qs -p settings.qml` process.
    // That is expensive: it loads a second copy of every service and widget
    // on top of the running shell (~400 MB of extra RSS). Here the window is
    // hosted inside the shell and opened over IPC, so it reuses everything
    // that is already in memory.
    Loader {
        id: settingsLoader
        active: false
        source: "settings.qml"
        onLoaded: {
            // Let the window know it is hosted by the shell, so closing it
            // does not terminate the whole shell.
            item.standalone = false;
            // A Loader does not manage the visibility of non-Item objects,
            // so the window has to be shown explicitly.
            item.show();
            item.raise();
            item.requestActivate();
            item.closing.connect(() => unloadSettingsTimer.restart());
        }
    }

    Timer {
        id: unloadSettingsTimer
        interval: 150
        // Free the window (and its memory) shortly after it was closed.
        onTriggered: settingsLoader.active = false
    }

    IpcHandler {
        target: "settings"

        // NOTE: the function must not be called "show" - quickshell does not
        // dispatch IPC calls to a function with that name.
        function open(): void {
            if (!settingsLoader.active) {
                settingsLoader.active = true;
            } else if (settingsLoader.item) {
                settingsLoader.item.show();
                settingsLoader.item.raise();
                settingsLoader.item.requestActivate();
            }
        }

        function close(): void {
            if (settingsLoader.item)
                settingsLoader.item.close();
        }
    }


    // Shortcuts
    IpcHandler {
        target: "panelFamily"

        function cycle(): void {
            root.cyclePanelFamily()
        }
    }

    GlobalShortcut {
        name: "panelFamilyCycle"
        description: "Cycles panel family"

        onPressed: root.cyclePanelFamily()
    }
}

