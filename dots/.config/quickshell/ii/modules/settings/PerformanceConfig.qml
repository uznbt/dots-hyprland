import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import qs.services
import qs.modules.common
import qs.modules.common.functions
import qs.modules.common.widgets

ContentPage {
    id: root
    forceWidth: true

    readonly property string helperScript: `${FileUtils.trimFileProtocol(Directories.scriptPath)}/performance/apply.sh`

    // Live state of the machine, refreshed while this page is open.
    property string governor: "powersave"
    property string energyPreference: "balance_power"
    property bool turboEnabled: true
    property int maxPerformancePercent: 100
    property int gpuMaxFrequencyMHz: 0
    property int gpuFrequencyPercent: 100
    property bool hasGpuFrequency: false
    property double ramTotalBytes: 0
    property double zramSizeBytes: 0
    property int zramSizePercent: 100
    property int swappiness: 60
    property bool blurEnabled: true

    property bool applying: false
    property bool pendingReboot: false
    property bool applyFailed: false
    property string applyError: ""

    // Controls report their initial value while they are created. Changes are
    // only forwarded once the page is fully built.
    property bool initialized: false

    // Sliders animate towards their new value, so they report a lot of
    // intermediate values. The last one is stored here and written to the
    // config once the animation settled.
    property real pendingZram: -1
    property real pendingSwappiness: -1
    property real pendingGpuFrequency: -1
    property real pendingCpuFrequency: -1

    readonly property string probeScript: [
        "gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)",
        "epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null)",
        "turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null)",
        "maxperf=$(cat /sys/devices/system/cpu/intel_pstate/max_perf_pct 2>/dev/null)",
        "mem=$(awk '/^MemTotal/{print $2}' /proc/meminfo 2>/dev/null)",
        "zram=$(cat /sys/block/zram0/disksize 2>/dev/null)",
        "swap=$(cat /proc/sys/vm/swappiness 2>/dev/null)",
        "gpu=$(ls /sys/class/drm/card*/gt_max_freq_mhz 2>/dev/null | head -n1)",
        "gpumax=$(cat $gpu 2>/dev/null)",
        "gpurb0=$(cat ${gpu%/*}/gt_RP0_freq_mhz 2>/dev/null)",
        "printf 'governor=%s\\nepp=%s\\nturbo=%s\\nmax_perf=%s\\nmem_kb=%s\\nzram=%s\\nswappiness=%s\\ngpu_max=%s\\ngpu_rp0=%s\\n' \"$gov\" \"$epp\" \"$turbo\" \"$maxperf\" \"$mem\" \"$zram\" \"$swap\" \"$gpumax\" \"$gpurb0\""
    ].join("; ");

    readonly property string stateSummary: {
        const ram = root.ramTotalBytes > 0 ? (root.ramTotalBytes / 1024 / 1024 / 1024).toFixed(1) + " GB" : "?";
        const zram = root.zramSizeBytes > 0 ? (root.zramSizeBytes / 1024 / 1024 / 1024).toFixed(1) + " GB" : "?";
        return Translation.tr("Currently: %1 RAM, %2 zram (%3%), swappiness %4, governor %5, boost %6")
            .arg(ram).arg(zram).arg(root.zramSizePercent).arg(root.swappiness)
            .arg(root.governor).arg(root.turboEnabled ? Translation.tr("Yes") : Translation.tr("No"));
    }

    function probe() {
        probeProc.running = true;
    }

    function handleProbe(output) {
        const values = {};
        for (const raw of output.split("\n")) {
            const line = raw.trim();
            if (line === "")
                continue;
            const separator = line.indexOf("=");
            if (separator === -1)
                continue;
            values[line.substring(0, separator)] = line.substring(separator + 1);
        }

        if (values.governor)
            root.governor = values.governor;
        if (values.epp)
            root.energyPreference = values.epp;
        if (values.turbo !== undefined && values.turbo !== "")
            root.turboEnabled = values.turbo !== "1";
        if (values.max_perf)
            root.maxPerformancePercent = parseInt(values.max_perf);
        if (values.mem_kb)
            root.ramTotalBytes = parseInt(values.mem_kb) * 1024;
        if (values.zram)
            root.zramSizeBytes = parseInt(values.zram);
        if (values.swappiness)
            root.swappiness = parseInt(values.swappiness);

        root.zramSizePercent = root.ramTotalBytes > 0
            ? Math.round((root.zramSizeBytes / root.ramTotalBytes) * 100)
            : 100;

        if (values.gpu_max) {
            root.gpuMaxFrequencyMHz = parseInt(values.gpu_max);
            root.hasGpuFrequency = true;
        }
        if (values.gpu_rp0 && root.gpuMaxFrequencyMHz > 0)
            root.gpuFrequencyPercent = Math.round((root.gpuMaxFrequencyMHz / parseInt(values.gpu_rp0)) * 100);
    }

    function applyBlur(enabled) {
        blurProc.command = ["hyprctl", "keyword", "decoration:blur:enabled", enabled ? "true" : "false"];
        blurProc.running = true;
    }

    // Sliders fire on every step, so the privileged calls are debounced:
    // the helper only runs once the user stops moving the slider.
    function requestMemory() {
        memoryTimer.restart();
    }

    function requestCpu() {
        cpuTimer.restart();
    }

    function requestGpuFrequency() {
        gpuTimer.restart();
    }

    // Writes the settled slider values to the config and applies them.
    function commitPending() {
        if (root.pendingZram >= 0) {
            Config.options.performance.memory.zramSizePercent = Math.round(root.pendingZram);
            root.pendingZram = -1;
            root.requestMemory();
        }
        if (root.pendingSwappiness >= 0) {
            Config.options.performance.memory.swappiness = Math.round(root.pendingSwappiness);
            root.pendingSwappiness = -1;
            root.requestMemory();
        }
        if (root.pendingGpuFrequency >= 0) {
            Config.options.performance.graphics.gpuFrequencyPercent = Math.round(root.pendingGpuFrequency);
            root.pendingGpuFrequency = -1;
            root.requestGpuFrequency();
        }
        if (root.pendingCpuFrequency >= 0) {
            Config.options.performance.cpu.maxFrequencyPercent = Math.round(root.pendingCpuFrequency);
            root.pendingCpuFrequency = -1;
            root.requestCpu();
        }
    }

    // The helper needs the admin password, so only one request runs at a time.
    // Everything that comes in meanwhile is queued.
    property var actionQueue: []

    function queueAction(args) {
        root.actionQueue.push(args);
        root.runNextAction();
    }

    function runNextAction() {
        if (root.applying || root.actionQueue.length === 0)
            return;

        root.applying = true;
        root.pendingReboot = false;
        root.applyFailed = false;
        root.applyError = "";
        const args = root.actionQueue.shift();
        helperProc.command = ["pkexec", root.helperScript, ...args];
        helperProc.running = true;
    }

    Process {
        id: probeProc
        command: ["bash", "-c", root.probeScript]
        stdout: StdioCollector {
            id: probeOutput
            onStreamFinished: {
                root.handleProbe(probeOutput.text);
            }
        }
    }

    Process {
        id: blurProc
        command: ["hyprctl", "getoption", "decoration:blur:enabled"]
        stdout: StdioCollector {
            id: blurOutput
            onStreamFinished: {
                const match = blurOutput.text.match(/bool:\s*(true|false)/);
                if (match) {
                    root.blurEnabled = match[1] === "true";
                    // Keep the stored setting in sync with the compositor.
                    if (Config.options.performance.graphics.blur !== root.blurEnabled)
                        Config.options.performance.graphics.blur = root.blurEnabled;
                }
            }
        }
    }

    Process {
        id: helperProc
        stdout: StdioCollector {
            id: helperStdout
            onStreamFinished: {
                for (const raw of helperStdout.text.split("\n")) {
                    const line = raw.trim();
                    if (line.startsWith("STATUS ")) {
                        const [key, value] = line.substring(7).split("=");
                        if (key === "zram" && value === "pending_reboot")
                            root.pendingReboot = true;
                    }
                }
            }
        }
        stderr: StdioCollector {
            id: helperStderr
            onStreamFinished: {
                let message = helperStderr.text.trim();
                if (message.startsWith("ERROR "))
                    message = message.substring(6);
                if (message.length > 0) {
                    root.applyFailed = true;
                    root.applyError = message.split("\n").pop();
                }
            }
        }
        onExited: (exitCode, exitStatus) => {
            root.applying = false;
            if (exitCode !== 0)
                root.applyFailed = true;
            root.probe();
            root.runNextAction();
        }
    }

    Timer {
        interval: 5000
        repeat: true
        running: true
        onTriggered: root.probe()
    }

    Timer {
        id: memoryTimer
        interval: 700
        onTriggered: root.queueAction([
            "memory",
            `zram_percent=${Config.options.performance.memory.zramSizePercent}`,
            `swappiness=${Config.options.performance.memory.swappiness}`
        ])
    }

    Timer {
        id: configTimer
        interval: 400
        onTriggered: root.commitPending()
    }

    Timer {
        id: cpuTimer
        interval: 700
        onTriggered: root.queueAction([
            "cpu",
            `profile=${Config.options.performance.cpu.profile}`,
            `boost=${Config.options.performance.cpu.boost ? 1 : 0}`,
            `max_perf=${Config.options.performance.cpu.maxFrequencyPercent}`
        ])
    }

    Timer {
        id: gpuTimer
        interval: 700
        onTriggered: root.queueAction([
            "graphics",
            `gpu_freq=${Config.options.performance.graphics.gpuFrequencyPercent}`
        ])
    }

    Component.onCompleted: {
        root.probe();
        blurProc.running = true;
        root.initialized = true;
    }

    NoticeBox {
        materialIcon: "shield"
        text: Translation.tr("Changing system settings asks for your administrator password. The zram size only changes after a reboot when memory is already tight.")
    }

    NoticeBox {
        showIcon: false
        text: root.stateSummary
    }

    NoticeBox {
        showIcon: false
        visible: root.pendingReboot
        text: Translation.tr("The new zram size is saved and is applied the next time the machine boots.")
    }

    NoticeBox {
        showIcon: false
        visible: root.applyFailed
        text: Translation.tr("The change could not be applied: %1").arg(root.applyError)
    }

    ContentSection {
        icon: "memory"
        title: Translation.tr("Memory (RAM)")

        ContentSubsection {
            title: Translation.tr("Chromium and Electron apps")
            tooltip: Translation.tr("Applies to Brave, Spotify and WhatsZan. The apps have to be closed completely for the new limits to be used.")
            ConfigSpinBox {
                icon: "web"
                text: Translation.tr("Renderer processes")
                value: Config.options.performance.memory.rendererLimit
                from: 1
                to: 16
                stepSize: 1
                onValueChanged: {
                    if (!root.initialized)
                        return;
                    Config.options.performance.memory.rendererLimit = value;
                }
            }
            ConfigSpinBox {
                icon: "data_usage"
                text: Translation.tr("JavaScript heap per renderer (MB)")
                value: Config.options.performance.memory.jsHeapMb
                from: 64
                to: 2048
                stepSize: 64
                onValueChanged: {
                    if (!root.initialized)
                        return;
                    Config.options.performance.memory.jsHeapMb = value;
                }
            }
            ConfigSpinBox {
                icon: "sd_storage"
                text: Translation.tr("Disk cache per app (MB)")
                value: Config.options.performance.memory.diskCacheMb
                from: 16
                to: 1024
                stepSize: 16
                onValueChanged: {
                    if (!root.initialized)
                        return;
                    Config.options.performance.memory.diskCacheMb = value;
                }
            }
        }

        ContentSubsection {
            title: Translation.tr("Swap (zram)")
            tooltip: Translation.tr("zram is compressed swap in RAM. A high swappiness keeps cold pages in the compressed swap instead of the disk.")
            ConfigSlider {
                buttonIcon: "swap_horiz"
                text: Translation.tr("zram size (% of RAM)")
                value: Config.options.performance.memory.zramSizePercent
                usePercentTooltip: false
                from: 25
                to: 100
                stopIndicatorValues: [50, 100]
                onValueChanged: {
                    if (!root.initialized)
                        return;
                    root.pendingZram = value;
                    configTimer.restart();
                }
            }
            ConfigSlider {
                buttonIcon: "tune"
                text: Translation.tr("Swappiness")
                value: Config.options.performance.memory.swappiness
                usePercentTooltip: false
                from: 0
                to: 200
                stopIndicatorValues: [60, 150]
                onValueChanged: {
                    if (!root.initialized)
                        return;
                    root.pendingSwappiness = value;
                    configTimer.restart();
                }
            }
        }
    }

    ContentSection {
        icon: "speed"
        title: Translation.tr("Graphics (VRAM)")

        ContentSubsection {
            title: Translation.tr("Compositor")
            tooltip: Translation.tr("Blur is the biggest user of GPU memory in Hyprland. Turning it off makes the desktop lighter, especially on integrated graphics.")
            ConfigSwitch {
                buttonIcon: "blur_on"
                text: Translation.tr("Blur")
                checked: Config.options.performance.graphics.blur
                onCheckedChanged: {
                    if (!root.initialized)
                        return;
                    Config.options.performance.graphics.blur = checked;
                    root.applyBlur(checked);
                }
            }
        }

        ContentSubsection {
            title: Translation.tr("Integrated GPU")
            tooltip: Translation.tr("Caps the frequency of the integrated GPU. Lower values mean less heat, less power and less GPU memory churn.")
            visible: root.hasGpuFrequency
            ConfigSlider {
                buttonIcon: "speed"
                text: Translation.tr("Maximum frequency (%)")
                value: Config.options.performance.graphics.gpuFrequencyPercent
                usePercentTooltip: false
                from: 50
                to: 100
                stopIndicatorValues: [100]
                onValueChanged: {
                    if (!root.initialized)
                        return;
                    root.pendingGpuFrequency = value;
                    configTimer.restart();
                }
            }
        }
    }

    ContentSection {
        icon: "bolt"
        title: Translation.tr("CPU")

        ContentSubsection {
            title: Translation.tr("Power profile")
            tooltip: Translation.tr("Balanced keeps the processor quiet while still reacting fast. Performance pins the maximum frequency, which uses more battery.")
            ConfigSelectionArray {
                currentValue: Config.options.performance.cpu.profile
                onSelected: newValue => {
                    if (!root.initialized)
                        return;
                    Config.options.performance.cpu.profile = newValue;
                    root.requestCpu();
                }
                options: [
                    {
                        displayName: Translation.tr("Battery saver"),
                        icon: "battery_saver",
                        value: "battery"
                    },
                    {
                        displayName: Translation.tr("Balanced"),
                        icon: "balance",
                        value: "balanced"
                    },
                    {
                        displayName: Translation.tr("Performance"),
                        icon: "speed",
                        value: "performance"
                    }
                ]
            }
        }

        ContentSubsection {
            title: Translation.tr("Turbo boost")
            tooltip: Translation.tr("Turbo boost lets the processor run above its base frequency. Turning it off lowers heat and power usage.")
            ConfigSwitch {
                buttonIcon: "bolt"
                text: Translation.tr("Turbo boost")
                checked: Config.options.performance.cpu.boost
                onCheckedChanged: {
                    if (!root.initialized)
                        return;
                    Config.options.performance.cpu.boost = checked;
                    root.requestCpu();
                }
            }
        }

        ContentSubsection {
            title: Translation.tr("Frequency limit")
            tooltip: Translation.tr("Limits the maximum frequency of the processor. 100% means the full range is available.")
            ConfigSlider {
                buttonIcon: "clock"
                text: Translation.tr("Maximum frequency (%)")
                value: Config.options.performance.cpu.maxFrequencyPercent
                usePercentTooltip: false
                from: 20
                to: 100
                stopIndicatorValues: [100]
                onValueChanged: {
                    if (!root.initialized)
                        return;
                    root.pendingCpuFrequency = value;
                    configTimer.restart();
                }
            }
        }
    }
}
