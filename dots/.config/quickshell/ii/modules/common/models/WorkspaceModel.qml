import QtQuick
import Quickshell.Wayland
import Quickshell.Hyprland
import qs.services
import qs.modules.common as C

NestableObject {
    id: root

    required property HyprlandMonitor monitor
    readonly property var liveMonitorData: HyprlandData.monitors.find(m => m.id === monitor.id)
    readonly property Toplevel activeWindow: ToplevelManager.activeToplevel
    readonly property int activeWorkspace: monitor?.activeWorkspace?.id ?? 1
    readonly property bool currentWorkspaceNotFake: activeWindow?.activated ?? false // Active empty workspace = fake. At least, that's how I like to call it.
    readonly property int fakeWorkspace: currentWorkspaceNotFake ? -9999 : activeWorkspace
    readonly property int shownCount: C.Config.options.bar.workspaces.shown
    readonly property int group: Math.floor((activeWorkspace - 1) / shownCount)
    readonly property var specialWorkspace: liveMonitorData?.specialWorkspace
    readonly property string specialWorkspaceName: specialWorkspace?.name.replace("special:", "") ?? "special"
    readonly property bool specialWorkspaceActive: specialWorkspaceName !== ""
    // Special workspace yang selalu ditampilkan di bar. Hyprland menghapus
    // special workspace yang kosong, jadi tanpa daftar ini ikonnya ikut
    // hilang begitu isinya kosong (mis. scratchpad "special").
    readonly property var knownSpecialWorkspaces: ["music", "special"]
    // Semua special workspace di monitor ini, digabung dengan daftar di atas.
    readonly property var specialWorkspaces: {
        const result = [];
        const add = name => {
            if (name && result.indexOf(name) === -1)
                result.push(name);
        };
        root.knownSpecialWorkspaces.forEach(add);
        Hyprland.workspaces.values.forEach(ws => {
            if ((ws.name ?? "").startsWith("special:") && (ws.monitor?.id ?? -1) === root.monitor.id)
                add(ws.name.replace("special:", ""));
        });
        return result.sort();
    }

    property list<bool> occupied: []
    property list<var> biggestWindow: occupied.map((_, index) => {
        const wsId = getWorkspaceIdAt(index);
        var biggestWindow = HyprlandData.biggestWindowForWorkspace(wsId);
        return biggestWindow;
    })

    function getWorkspaceId(group, index) {
        return group * root.shownCount + index + 1;
    }
    function getWorkspaceIdAt(index) {
        return root.getWorkspaceId(root.group, index);
    }

    // Function to update workspaceOccupied
    function updateWorkspaceOccupied() {
        root.occupied = Array.from({
            length: root.shownCount
        }, (_, i) => {
            const thisWorkspaceId = getWorkspaceId(root.group, i);
            return Hyprland.workspaces.values.some(ws => ws.id === thisWorkspaceId);
        });
    }

    // Occupied workspace updates
    Component.onCompleted: updateWorkspaceOccupied()
    Connections {
        target: Hyprland.workspaces
        function onValuesChanged() {
            root.updateWorkspaceOccupied();
        }
    }
    Connections {
        target: Hyprland
        function onFocusedWorkspaceChanged() {
            root.updateWorkspaceOccupied();
        }
    }
    onGroupChanged: {
        updateWorkspaceOccupied();
    }
}
