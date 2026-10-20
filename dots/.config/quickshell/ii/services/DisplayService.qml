pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import qs.modules.common
import qs.services

/**
 * Small wrapper around Hyprland's monitor/workspace commands for the
 * Super+P display panel.
 *
 * Hyprland 0.56 uses the Lua config, so monitor changes go through
 * `hyprctl eval` instead of the old `hyprctl keyword monitor` syntax.
 */
Singleton {
    id: root

    property var monitors: []
    property var activeMonitorNames: []
    property bool loading: false
    property string lastError: ""
    property string lastAction: ""
    property string selectedMonitorName: ""
    property string workspaceTargetName: ""

    readonly property var activeMonitors: {
        // `monitors all` also reports outputs that are connected but not
        // currently driving a surface. `monitors` is the authoritative active
        // list, so use it when available.
        if (activeMonitorNames.length > 0)
            return monitors.filter(monitor => activeMonitorNames.includes(monitor.name));
        return monitors.filter(monitor => !monitor.disabled);
    }
    readonly property var internalMonitor: {
        const isInternal = monitor => /^(eDP|LVDS|DSI|edp)/i.test(monitor.name ?? "");
        return activeMonitors.find(isInternal) ?? monitors.find(isInternal) ?? null;
    }
    readonly property var externalMonitors: activeMonitors.filter(monitor =>
        !internalMonitor || monitor.name !== internalMonitor.name)
    readonly property var allExternalMonitors: monitors.filter(monitor =>
        !internalMonitor || monitor.name !== internalMonitor.name)
    readonly property bool hasInternal: internalMonitor !== null
    readonly property bool hasExternal: externalMonitors.length > 0
    readonly property bool hasConnectedExternal: allExternalMonitors.length > 0
    readonly property var selectedMonitor: monitors.find(monitor =>
        monitor.name === selectedMonitorName) ?? externalMonitors[0] ?? internalMonitor ?? activeMonitors[0] ?? null
    readonly property var workspaceTarget: activeMonitors.find(monitor =>
        monitor.name === workspaceTargetName) ?? externalMonitors[0] ?? activeMonitors[0] ?? null
    readonly property int workspaceCount: Math.max(1, Config.options.bar.workspaces.shown || 10)
    readonly property var workspaces: HyprlandData.workspaces

    readonly property string displayMode: {
        const internal = internalMonitor;
        const internalActive = internal && !internal.disabled;
        const externals = externalMonitors;

        if (!internal)
            return externals.length > 0 ? "external" : "internal";

        if (!internalActive && externals.length > 0)
            return "external";

        if (internalActive && externals.length > 0) {
            if (externals.some(monitor => monitor.mirrorOf === internal.name
                    || String(monitor.mirrorOf) === String(internal.id)))
                return "duplicate";
            return "extend";
        }

        return "internal";
    }

    readonly property var modeOptions: {
        const options = [];
        if (hasInternal) {
            options.push({ displayName: "Internal", icon: "laptop_mac", value: "internal" });
            options.push({ displayName: "Duplikat", icon: "content_copy", value: "duplicate" });
        }
        options.push({ displayName: "Perluas", icon: "open_in_new", value: "extend" });
        options.push({ displayName: "Eksternal", icon: "monitor", value: "external" });
        return options;
    }

    function modeLabel(mode) {
        switch (mode) {
        case "internal":
            return "Internal";
        case "duplicate":
            return "Duplikat";
        case "extend":
            return "Perluas";
        case "external":
            return "Eksternal";
        default:
            return mode;
        }
    }

    function luaString(value) {
        return JSON.stringify(String(value));
    }

    function monitorForName(name) {
        return monitors.find(monitor => monitor.name === name) ?? null;
    }

    function modesFor(monitor) {
        if (!monitor || !monitor.availableModes || monitor.availableModes.length === 0)
            return ["preferred"];

        const seen = new Set();
        const result = [];
        for (const rawMode of monitor.availableModes) {
            const mode = String(rawMode).replace(/Hz$/, "");
            if (!seen.has(mode)) {
                seen.add(mode);
                result.push(mode);
            }
        }
        return result.length > 0 ? result : ["preferred"];
    }

    function currentMode(monitor) {
        if (!monitor || monitor.disabled || !monitor.width || !monitor.height)
            return "preferred";

        const modes = modesFor(monitor);
        const exact = modes.find(mode => {
            if (!mode.startsWith(`${monitor.width}x${monitor.height}@`))
                return false;
            const refresh = parseFloat(mode.split("@")[1]);
            return Math.abs(refresh - monitor.refreshRate) < 0.2;
        });
        if (exact)
            return exact;

        return modes.find(mode => mode.startsWith(`${monitor.width}x${monitor.height}@`)) ?? "preferred";
    }

    function currentPosition(monitor) {
        if (!monitor || monitor.disabled)
            return "auto";
        return `${monitor.x ?? 0}x${monitor.y ?? 0}`;
    }

    function mirrorFor(monitor) {
        if (!monitor || !monitor.mirrorOf || monitor.mirrorOf === "none")
            return "";
        return monitor.mirrorOf;
    }

    function validScales(monitor) {
        if (!monitor || !monitor.width || !monitor.height)
            return [1];

        const candidates = [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2];
        const valid = candidates.filter(scale => {
            const width = monitor.width / scale;
            const height = monitor.height / scale;
            return Math.abs(width - Math.round(width)) < 0.01 && Math.abs(height - Math.round(height)) < 0.01;
        });

        const current = Number(monitor.scale) || 1;
        if (!valid.some(scale => Math.abs(scale - current) < 0.001))
            valid.push(current);

        valid.sort((a, b) => a - b);
        return valid;
    }

    function buildMonitorRule(monitor, overrides) {
        if (!monitor)
            return "";

        const options = overrides ?? {};
        const fields = [`output = ${luaString(monitor.name)}`];

        if (options.disabled) {
            fields.push("disabled = true");
        } else {
            const mode = options.mode ?? currentMode(monitor);
            const position = options.position ?? currentPosition(monitor);
            const scale = options.scale ?? monitor.scale ?? 1;
            const transform = options.transform ?? monitor.transform ?? 0;
            const mirror = options.mirror !== undefined ? options.mirror : mirrorFor(monitor);

            fields.push(`mode = ${luaString(mode)}`);
            fields.push(`position = ${luaString(position)}`);
            fields.push(`scale = ${Number(scale)}`);
            fields.push(`transform = ${Number(transform)}`);
            if (mirror)
                fields.push(`mirror = ${luaString(mirror)}`);
        }

        return `hl.monitor({ ${fields.join(", ")} })`;
    }

    function buildMirrorRule(monitor, targetMonitor) {
        if (!monitor || !targetMonitor)
            return "";
        return `hl.monitor({ output = ${luaString(monitor.name)}, mirror = ${luaString(targetMonitor)} })`;
    }

    function runEval(code) {
        if (!code || code.length === 0)
            return;

        lastError = "";
        evalProcess.command = ["hyprctl", "eval", code];
        if (evalProcess.running)
            evalProcess.running = false;
        evalProcess.running = true;
    }

    function refresh() {
        loading = true;
        if (monitorsProcess.running)
            monitorsProcess.running = false;
        monitorsProcess.running = true;
        if (activeMonitorsProcess.running)
            activeMonitorsProcess.running = false;
        activeMonitorsProcess.running = true;
        HyprlandData.updateWorkspaces();
    }

    function ensureSelection() {
        if (!monitors.some(monitor => monitor.name === selectedMonitorName)) {
            selectedMonitorName = externalMonitors[0]?.name ?? internalMonitor?.name ?? monitors[0]?.name ?? "";
        }

        if (!activeMonitors.some(monitor => monitor.name === workspaceTargetName)) {
            workspaceTargetName = externalMonitors[0]?.name ?? activeMonitors[0]?.name ?? "";
        }
    }

    function applyDisplayMode(mode) {
        const internal = internalMonitor;
        // Only currently active monitors are safe to use for destructive modes.
        // Disabling the last active output would make Hyprland recreate outputs
        // (or fall back to a virtual monitor), so never do that.
        const externals = externalMonitors;
        const needsExternal = mode === "duplicate" || mode === "extend" || mode === "external";

        if (needsExternal && externals.length === 0) {
            lastError = "Monitor eksternal belum aktif. Aktifkan monitor eksternal terlebih dahulu.";
            return false;
        }
        if ((mode === "duplicate" || mode === "internal") && !internal) {
            lastError = "Monitor internal tidak terdeteksi.";
            return false;
        }
        if (mode === "internal" && internal.disabled) {
            lastError = "Aktifkan monitor internal terlebih dahulu lewat daftar monitor.";
            return false;
        }

        const rules = [];
        const enableInternal = () => {
            if (!internal)
                return;
            rules.push(buildMonitorRule(internal, {
                mode: currentMode(internal),
                // Re-enable to the left of the active external output so we
                // never create an overlap while the external is still on.
                position: externals.length > 0 ? "auto-left" : "auto",
                scale: internal.scale || 1,
                transform: internal.transform ?? 0,
                mirror: null
            }));
        };
        const enableExternal = (monitor, position) => {
            rules.push(buildMonitorRule(monitor, {
                mode: currentMode(monitor),
                position: position,
                scale: monitor.scale || 1,
                transform: monitor.transform ?? 0,
                mirror: null
            }));
        };

        if (mode === "internal") {
            for (const monitor of externals)
                rules.push(buildMonitorRule(monitor, { disabled: true }));
        } else if (mode === "external") {
            // The external output is already active; only turn off the panel.
            if (internal)
                rules.push(buildMonitorRule(internal, { disabled: true }));
        } else if (mode === "duplicate") {
            if (internal.disabled)
                enableInternal();
            for (const monitor of externals)
                rules.push(buildMirrorRule(monitor, internal.name));
        } else if (mode === "extend") {
            if (internal.disabled)
                enableInternal();
            externals.forEach((monitor, index) => {
                enableExternal(monitor, index === 0 ? "auto-right" : "auto");
            });
        } else {
            lastError = "Mode tampilan tidak dikenal.";
            return false;
        }

        if (rules.length === 0) {
            lastError = "Tidak ada perubahan monitor yang perlu diterapkan.";
            return false;
        }

        lastAction = `Mode ${modeLabel(mode)} diterapkan.`;
        runEval(rules.join("\n"));
        return true;
    }

    function updateSelectedMonitor(overrides) {
        const monitor = selectedMonitor;
        if (!monitor) {
            lastError = "Pilih monitor terlebih dahulu.";
            return;
        }
        if (monitor.disabled) {
            lastError = "Aktifkan monitor ini terlebih dahulu.";
            return;
        }

        lastAction = `Pengaturan ${monitor.name} diperbarui.`;
        runEval(buildMonitorRule(monitor, overrides));
    }

    function setSelectedMonitorMode(mode) {
        updateSelectedMonitor({ mode: mode });
    }

    function setSelectedMonitorScale(scale) {
        updateSelectedMonitor({ scale: Number(scale) });
    }

    function setSelectedMonitorTransform(transform) {
        updateSelectedMonitor({ transform: Number(transform) });
    }

    function setSelectedMonitorPosition(position) {
        updateSelectedMonitor({ position: position });
    }

    function enableSelectedMonitor() {
        const monitor = selectedMonitor;
        if (!monitor) {
            lastError = "Pilih monitor terlebih dahulu.";
            return;
        }

        lastAction = `${monitor.name} diaktifkan.`;
        runEval(buildMonitorRule(monitor, {
            disabled: false,
            mode: "preferred",
            position: "auto",
            scale: 1,
            transform: 0,
            mirror: null
        }));
    }

    function disableSelectedMonitor() {
        const monitor = selectedMonitor;
        if (!monitor) {
            lastError = "Pilih monitor terlebih dahulu.";
            return;
        }
        if (activeMonitors.length <= 1) {
            lastError = "Minimal satu monitor harus tetap aktif.";
            return;
        }

        lastAction = `${monitor.name} dinonaktifkan.`;
        runEval(buildMonitorRule(monitor, { disabled: true }));
    }

    function workspaceMonitorName(workspaceId) {
        return HyprlandData.workspaceById[workspaceId]?.monitor ?? "";
    }

    function showWorkspaceOnMonitor(workspaceId, monitorName) {
        const monitor = activeMonitors.find(item => item.name === monitorName);
        if (!monitor) {
            lastError = "Pilih monitor tujuan terlebih dahulu.";
            return;
        }

        const id = String(workspaceId);
        const exists = !!HyprlandData.workspaceById[workspaceId];
        const lines = [
            `hl.workspace_rule({ workspace = ${luaString(id)}, monitor = ${luaString(monitorName)} })`
        ];

        if (exists) {
            lines.push(`hl.dispatch(hl.dsp.workspace.move({ workspace = ${luaString(id)}, monitor = ${luaString(monitorName)} }))`);
        } else {
            lines.push(`hl.dispatch(hl.dsp.focus({ monitor = ${luaString(monitorName)} }))`);
        }
        lines.push(`hl.dispatch(hl.dsp.focus({ workspace = ${Number(workspaceId)} }))`);

        lastAction = `Workspace ${workspaceId} ditampilkan di ${monitorName}.`;
        runEval(lines.join("\n"));
    }

    Process {
        id: activeMonitorsProcess
        command: ["hyprctl", "monitors", "-j"]

        stdout: StdioCollector {
            id: activeMonitorsCollector
            onStreamFinished: {
                try {
                    const parsed = JSON.parse(activeMonitorsCollector.text);
                    if (!Array.isArray(parsed))
                        throw new Error("Unexpected active monitor data");
                    root.activeMonitorNames = parsed.map(monitor => monitor.name);
                    root.ensureSelection();
                } catch (error) {
                    console.error("[DisplayService] Failed to parse active monitors:", error);
                }
            }
        }
    }

    Process {
        id: monitorsProcess
        command: ["hyprctl", "monitors", "all", "-j"]

        stdout: StdioCollector {
            id: monitorsCollector
            onStreamFinished: {
                try {
                    const parsed = JSON.parse(monitorsCollector.text);
                    if (!Array.isArray(parsed))
                        throw new Error("Unexpected monitor data");
                    root.monitors = parsed;
                    root.loading = false;
                    root.lastError = "";
                    root.ensureSelection();
                } catch (error) {
                    root.loading = false;
                    root.lastError = "Gagal membaca data monitor.";
                    console.error("[DisplayService] Failed to parse monitors:", error);
                }
            }
        }

        onExited: (exitCode, exitStatus) => {
            root.loading = false;
            if (exitCode !== 0)
                root.lastError = "Gagal menjalankan hyprctl.";
        }
    }

    Process {
        id: evalProcess
        command: ["hyprctl", "eval", ""]

        stdout: StdioCollector {
            id: evalCollector
            onStreamFinished: {
                const output = evalCollector.text.trim();
                if (output.startsWith("error:") || output.includes("\nerror:"))
                    root.lastError = output;
            }
        }

        onExited: (exitCode, exitStatus) => {
            if (exitCode !== 0 && root.lastError.length === 0)
                root.lastError = "Perintah Hyprland gagal dijalankan.";
            refreshDebounce.restart();
        }
    }

    Timer {
        id: refreshDebounce
        interval: 500
        repeat: false
        onTriggered: root.refresh()
    }

    Connections {
        target: Hyprland
        function onRawEvent(event) {
            const names = [
                "monitoradded", "monitoraddedv2", "monitorremoved", "monitorremovedv2",
                "focusedmon", "workspace", "workspacev2", "createworkspace", "createworkspacev2",
                "destroyworkspace", "destroyworkspacev2", "moveworkspace", "moveworkspacev2"
            ];
            if (names.includes(event.name))
                refreshDebounce.restart();
        }
    }

    Component.onCompleted: refresh()
}
