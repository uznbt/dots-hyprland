import qs
import qs.services
import qs.modules.common
import qs.modules.common.widgets
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland

// Indikator timer melayang. Muncul selama pomodoro / stopwatch jalan (atau
// lagi di-pause di tengah jalan), supaya tetap kelihatan walau sidebar kanan
// ditutup dan lagi buka aplikasi lain.
Scope {
    id: root

    // Timer dianggap "ada" kalau sedang jalan, atau sudah jalan lalu di-pause
    // (detiknya berkurang dari durasi awal).
    readonly property bool pomodoroActive: TimerService.pomodoroRunning || TimerService.pomodoroSecondsLeft < TimerService.pomodoroLapDuration
    readonly property bool stopwatchActive: TimerService.stopwatchRunning || TimerService.stopwatchTime > 0
    readonly property bool showStopwatch: root.stopwatchActive && !root.pomodoroActive
    readonly property bool shouldShow: (root.pomodoroActive || root.stopwatchActive) && !GlobalStates.sidebarRightOpen && !GlobalStates.screenLocked

    readonly property bool running: root.showStopwatch ? TimerService.stopwatchRunning : TimerService.pomodoroRunning
    readonly property real progress: root.showStopwatch ? 1 : Math.max(0, Math.min(1, TimerService.pomodoroSecondsLeft / Math.max(1, TimerService.pomodoroLapDuration)))

    function formatSeconds(seconds) {
        const total = Math.max(0, Math.floor(seconds));
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60).toString().padStart(2, "0");
        const s = (total % 60).toString().padStart(2, "0");
        return h > 0 ? `${h}:${m}:${s}` : `${m}:${s}`;
    }

    readonly property string timeText: root.showStopwatch ? formatSeconds(TimerService.stopwatchTime / 100) : formatSeconds(TimerService.pomodoroSecondsLeft)

    readonly property string labelText: {
        if (root.showStopwatch)
            return Translation.tr("Stopwatch");
        if (TimerService.pomodoroLongBreak)
            return Translation.tr("Long break");
        if (TimerService.pomodoroBreak)
            return Translation.tr("Break");
        return Translation.tr("Focus");
    }

    function openTimer() {
        Persistent.states.sidebar.bottomGroup.tab = 2;
        Persistent.states.sidebar.bottomGroup.collapsed = false;
        GlobalStates.sidebarRightOpen = true;
    }

    function toggleRunning() {
        if (root.showStopwatch)
            TimerService.toggleStopwatch();
        else
            TimerService.togglePomodoro();
    }

    PanelWindow {
        id: panelWindow

        visible: root.shouldShow
        color: "transparent"
        exclusionMode: ExclusionMode.Ignore
        exclusiveZone: 0
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.namespace: "quickshell:timerIndicator"

        anchors {
            bottom: true
            right: true
        }
        margins {
            bottom: Appearance.sizes.hyprlandGapsOut + Appearance.sizes.elevationMargin + (Config.options.bar.bottom && !Config.options.bar.vertical ? Appearance.sizes.barHeight : 0)
            right: Appearance.sizes.hyprlandGapsOut + Appearance.sizes.elevationMargin + (Config.options.bar.vertical && Config.options.bar.bottom ? Appearance.sizes.verticalBarWidth : 0)
        }

        implicitWidth: pill.implicitWidth + Appearance.sizes.elevationMargin * 2
        implicitHeight: pill.implicitHeight + Appearance.sizes.elevationMargin * 2

        mask: Region {
            item: pill
        }

        StyledRectangularShadow {
            target: pill
        }

        Rectangle {
            id: pill

            anchors.centerIn: parent
            implicitWidth: contentRow.implicitWidth + 24
            implicitHeight: 46
            radius: Appearance.rounding.full
            color: Appearance.m3colors.m3surfaceContainer
            border.width: 1
            border.color: Appearance.colors.colLayer0Border

            // Klik di mana aja (kecuali tombol pause) buka sidebar di tab Timer.
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.openTimer()
            }

            RowLayout {
                id: contentRow

                anchors.centerIn: parent
                spacing: 10

                Item {
                    implicitWidth: 30
                    implicitHeight: 30

                    CircularProgress {
                        anchors.fill: parent
                        visible: !root.showStopwatch
                        implicitSize: 30
                        lineWidth: 3
                        value: root.progress
                        enableAnimation: true
                    }

                    MaterialSymbol {
                        anchors.centerIn: parent
                        visible: root.showStopwatch
                        text: "timer"
                        iconSize: 22
                        color: Appearance.colors.colOnSurfaceVariant
                    }
                }

                ColumnLayout {
                    spacing: -3

                    StyledText {
                        text: root.timeText
                        color: Appearance.colors.colOnSurface
                        font.pixelSize: Appearance.font.pixelSize.large
                        font.weight: Font.Medium
                    }
                    StyledText {
                        text: root.labelText
                        color: Appearance.colors.colSubtext
                        font.pixelSize: Appearance.font.pixelSize.smaller
                    }
                }

                Rectangle {
                    implicitWidth: 28
                    implicitHeight: 28
                    radius: 14
                    color: pauseArea.containsMouse ? Appearance.colors.colLayer1Hover : "transparent"

                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: root.running ? "pause" : "play_arrow"
                        iconSize: 20
                        color: Appearance.colors.colOnSurfaceVariant
                    }

                    MouseArea {
                        id: pauseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleRunning()
                    }
                }
            }
        }
    }

    // Kontrol timer lewat IPC, biar bisa dipakai keybind / script juga.
    IpcHandler {
        target: "timer"

        function pomodoro(): void {
            TimerService.togglePomodoro();
        }

        function stopwatch(): void {
            TimerService.toggleStopwatch();
        }

        function reset(): void {
            if (root.showStopwatch)
                TimerService.stopwatchReset();
            else
                TimerService.resetPomodoro();
        }

        function open(): void {
            root.openTimer();
        }
    }
}
