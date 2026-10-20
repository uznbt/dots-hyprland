import qs.services
import qs.modules.common
import qs.modules.common.widgets
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell

Item {
    id: root

    readonly property var selectedMonitor: DisplayService.selectedMonitor
    readonly property var workspaceTarget: DisplayService.workspaceTarget

    implicitWidth: background.implicitWidth
    implicitHeight: background.implicitHeight


    StyledRectangularShadow {
        target: background
    }

    Rectangle {
        id: background
        anchors.fill: parent
        color: Appearance.colors.colLayer0Base
        clip: true
        border.width: 1
        border.color: Appearance.colors.colLayer0Border
        radius: Appearance.rounding.screenRounding - Appearance.sizes.hyprlandGapsOut + 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            RowLayout {
                id: headerRow
                Layout.fillWidth: true
                // Nested layouts otherwise cap their width at the implicit size,
                // which pushes the window buttons toward the middle.
                Layout.maximumWidth: Number.POSITIVE_INFINITY
                Layout.fillHeight: false
                Layout.preferredHeight: 44
                spacing: 10

                Rectangle {
                    implicitWidth: 44
                    implicitHeight: 44
                    radius: Appearance.rounding.normal
                    color: Appearance.colors.colPrimaryContainer

                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "monitor"
                        iconSize: 24
                        color: Appearance.colors.colOnPrimaryContainer
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Number.POSITIVE_INFINITY
                    spacing: 0

                    StyledText {
                        text: "Layar"
                        font.pixelSize: Appearance.font.pixelSize.larger
                        font.weight: Font.Medium
                        color: Appearance.colors.colOnLayer0
                    }

                    StyledText {
                        text: "Monitor & workspace"
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colOnLayer1Inactive
                    }
                }

                IconToolbarButton {
                    text: "refresh"
                    onClicked: DisplayService.refresh()
                }

                IconToolbarButton {
                    text: "close"
                    onClicked: GlobalStates.displayPanelOpen = false
                }
            }

            StyledFlickable {
                id: contentFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: contentColumn.implicitHeight + 4
                clip: true

                ColumnLayout {
                    id: contentColumn
                    width: parent.width
                    spacing: 12

                    SectionHeader {
                        title: "Mode tampilan"
                        icon: "cast"
                    }

                    NoticeBox {
                        Layout.fillWidth: true
                        visible: !DisplayService.hasExternal
                        showIcon: false
                        text: DisplayService.hasConnectedExternal
                            ? "Monitor eksternal terdeteksi tetapi belum aktif. Aktifkan dari daftar monitor di bawah."
                            : "Monitor eksternal belum terdeteksi. Sambungkan monitor lalu muat ulang."
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: DisplayService.hasExternal
                        spacing: 6

                        ConfigSelectionArray {
                            Layout.fillWidth: true
                            options: DisplayService.modeOptions
                            currentValue: DisplayService.displayMode
                            onSelected: value => DisplayService.applyDisplayMode(value)
                        }

                        StyledText {
                            Layout.fillWidth: true
                            text: DisplayService.hasInternal
                                ? "Pilih cara layar internal dan monitor eksternal ditampilkan."
                                : "Pilih bagaimana monitor eksternal digunakan."
                            color: Appearance.colors.colSubtext
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            wrapMode: Text.WordWrap
                        }
                    }

                    SectionHeader {
                        title: "Monitor"
                        icon: "devices"
                    }

                    Repeater {
                        model: DisplayService.monitors

                        delegate: MonitorCard {
                            required property var modelData
                            monitor: modelData
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: settingsColumn.implicitHeight + 24
                        visible: root.selectedMonitor !== null
                        radius: Appearance.rounding.normal
                        color: Appearance.colors.colLayer1
                        border.width: 1
                        border.color: Appearance.colors.colLayer0Border

                        ColumnLayout {
                            id: settingsColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                MaterialSymbol {
                                    text: "tune"
                                    iconSize: Appearance.font.pixelSize.larger
                                    color: Appearance.colors.colOnLayer1
                                }

                                StyledText {
                                    Layout.fillWidth: true
                                    text: root.selectedMonitor ? `Atur ${root.selectedMonitor.name}` : "Atur monitor"
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                    font.weight: Font.Medium
                                    color: Appearance.colors.colOnLayer1
                                    elide: Text.ElideRight
                                }

                                StyledText {
                                    visible: root.selectedMonitor?.disabled ?? false
                                    text: "Nonaktif"
                                    font.pixelSize: Appearance.font.pixelSize.smaller
                                    color: Appearance.colors.colOnLayer1Inactive
                                }
                            }

                            StyledText {
                                Layout.fillWidth: true
                                visible: root.selectedMonitor?.disabled ?? false
                                text: "Monitor ini sedang nonaktif. Aktifkan untuk mengubah resolusi dan pengaturannya."
                                color: Appearance.colors.colSubtext
                                font.pixelSize: Appearance.font.pixelSize.small
                                wrapMode: Text.WordWrap
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                visible: !(root.selectedMonitor?.disabled ?? true)
                                columns: 2
                                columnSpacing: 8
                                rowSpacing: 8

                                DisplayComboBox {
                                    buttonIcon: "aspect_ratio"
                                    textRole: "text"
                                    model: {
                                        if (!root.selectedMonitor)
                                            return [];
                                        return [{
                                            text: "Preferensi monitor",
                                            value: "preferred"
                                        }].concat(DisplayService.modesFor(root.selectedMonitor).map(mode => ({
                                            text: `${mode.split("@")[0].replace("x", "×")} · ${parseFloat(mode.split("@")[1])} Hz`,
                                            value: mode
                                        })));
                                    }
                                    currentIndex: {
                                        if (!root.selectedMonitor)
                                            return 0;
                                        const index = model.findIndex(item => item.value === DisplayService.currentMode(root.selectedMonitor));
                                        return index >= 0 ? index : 0;
                                    }
                                    onActivated: index => DisplayService.setSelectedMonitorMode(model[index].value)
                                }

                                DisplayComboBox {
                                    buttonIcon: "zoom_in"
                                    textRole: "text"
                                    model: root.selectedMonitor
                                        ? DisplayService.validScales(root.selectedMonitor).map(scale => ({
                                            text: scale + "×",
                                            value: scale
                                        }))
                                        : []
                                    currentIndex: {
                                        if (!root.selectedMonitor)
                                            return 0;
                                        const index = model.findIndex(item => Math.abs(Number(item.value) - Number(root.selectedMonitor.scale)) < 0.001);
                                        return index >= 0 ? index : 0;
                                    }
                                    onActivated: index => DisplayService.setSelectedMonitorScale(model[index].value)
                                }

                                DisplayComboBox {
                                    buttonIcon: "rotate_right"
                                    textRole: "text"
                                    model: [
                                        { text: "Normal", value: 0 },
                                        { text: "90°", value: 1 },
                                        { text: "180°", value: 2 },
                                        { text: "270°", value: 3 },
                                        { text: "Flipped", value: 4 },
                                        { text: "Flipped 90°", value: 5 },
                                        { text: "Flipped 180°", value: 6 },
                                        { text: "Flipped 270°", value: 7 }
                                    ]
                                    currentIndex: {
                                        if (!root.selectedMonitor)
                                            return 0;
                                        const index = model.findIndex(item => item.value === root.selectedMonitor.transform);
                                        return index >= 0 ? index : 0;
                                    }
                                    onActivated: index => DisplayService.setSelectedMonitorTransform(model[index].value)
                                }

                                DisplayComboBox {
                                    buttonIcon: "open_with"
                                    textRole: "text"
                                    model: {
                                        if (!root.selectedMonitor)
                                            return [];
                                        const options = [
                                            { text: "Otomatis", value: "auto" },
                                            { text: "Kanan otomatis", value: "auto-right" },
                                            { text: "Kiri otomatis", value: "auto-left" },
                                            { text: "Atas otomatis", value: "auto-up" },
                                            { text: "Bawah otomatis", value: "auto-down" },
                                            { text: "Kiri atas (0x0)", value: "0x0" }
                                        ];
                                        if (!root.selectedMonitor.disabled) {
                                            const current = `${root.selectedMonitor.x}x${root.selectedMonitor.y}`;
                                            if (!options.some(option => option.value === current))
                                                options.unshift({ text: `Saat ini (${current})`, value: current });
                                        }
                                        return options;
                                    }
                                    currentIndex: {
                                        if (!root.selectedMonitor)
                                            return 0;
                                        const current = root.selectedMonitor.disabled
                                            ? "auto"
                                            : `${root.selectedMonitor.x}x${root.selectedMonitor.y}`;
                                        const index = model.findIndex(item => item.value === current);
                                        if (index >= 0)
                                            return index;
                                        const autoIndex = model.findIndex(item => item.value === "auto");
                                        return autoIndex >= 0 ? autoIndex : 0;
                                    }
                                    onActivated: index => DisplayService.setSelectedMonitorPosition(model[index].value)
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                RippleButtonWithIcon {
                                    Layout.fillWidth: true
                                    visible: root.selectedMonitor?.disabled ?? false
                                    mainText: "Aktifkan monitor"
                                    materialIcon: "power_settings_new"
                                    onClicked: DisplayService.enableSelectedMonitor()
                                }

                                RippleButtonWithIcon {
                                    Layout.fillWidth: true
                                    visible: !(root.selectedMonitor?.disabled ?? true)
                                        && root.selectedMonitor?.name !== DisplayService.internalMonitor?.name
                                    mainText: "Nonaktifkan monitor"
                                    materialIcon: "power_settings_new"
                                    onClicked: DisplayService.disableSelectedMonitor()
                                }
                            }
                        }
                    }

                    SectionHeader {
                        id: workspaceHeader
                        title: "Workspace"
                        icon: "grid_view"
                    }

                    StyledText {
                        Layout.fillWidth: true
                        text: "Pilih monitor tujuan, lalu klik nomor workspace untuk menampilkannya di sana."
                        color: Appearance.colors.colSubtext
                        font.pixelSize: Appearance.font.pixelSize.small
                        wrapMode: Text.WordWrap
                    }

                    DisplayComboBox {
                        Layout.fillWidth: true
                        buttonIcon: "monitor"
                        textRole: "text"
                        model: DisplayService.activeMonitors.map(monitor => ({
                            text: `${monitor.name} · ${monitor.width}×${monitor.height}`,
                            value: monitor.name
                        }))
                        currentIndex: {
                            const index = model.findIndex(item => item.value === DisplayService.workspaceTargetName);
                            return index >= 0 ? index : 0;
                        }
                        onActivated: index => {
                            DisplayService.workspaceTargetName = model[index].value;
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 6
                        columnSpacing: 6
                        rowSpacing: 6

                        Repeater {
                            model: DisplayService.workspaceCount

                            delegate: WorkspaceButton {
                                required property int index
                                workspaceId: index + 1
                            }
                        }
                    }

                    StyledText {
                        Layout.fillWidth: true
                        text: "Workspace yang belum ada akan dibuat di monitor tujuan."
                        color: Appearance.colors.colSubtext
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        wrapMode: Text.WordWrap
                    }

                    StyledText {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: DisplayService.lastError.length > 0 ? DisplayService.lastError : DisplayService.lastAction
                        color: DisplayService.lastError.length > 0
                            ? Appearance.colors.colError
                            : Appearance.colors.colSubtext
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        wrapMode: Text.WordWrap
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                    }
                }
            }
        }
    }

    component DisplayComboBox: StyledComboBox {
        // Material Symbols sit a little low compared with the text baseline.
        buttonIconVerticalOffset: -2
    }

    component SectionHeader: RowLayout {
        required property string title
        required property string icon

        Layout.fillWidth: true
        spacing: 8

        MaterialSymbol {
            text: parent.icon
            iconSize: Appearance.font.pixelSize.larger
            color: Appearance.colors.colOnSecondaryContainer
        }

        StyledText {
            Layout.fillWidth: true
            text: parent.title
            font.pixelSize: Appearance.font.pixelSize.normal
            font.weight: Font.Medium
            color: Appearance.colors.colOnLayer0
        }
    }

    component MonitorCard: RippleButton {
        id: monitorCard
        required property var monitor

        readonly property bool selected: DisplayService.selectedMonitorName === monitor.name

        Layout.fillWidth: true
        // Three text lines plus equal top/bottom padding.
        implicitHeight: 90
        buttonRadius: Appearance.rounding.normal
        opacity: monitor.disabled ? 0.72 : 1
        colBackground: selected ? Appearance.colors.colPrimaryContainer : Appearance.colors.colLayer1
        colBackgroundHover: selected ? Appearance.colors.colPrimaryContainerHover : Appearance.colors.colLayer1Hover
        onClicked: DisplayService.selectedMonitorName = monitor.name

        contentItem: Item {
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                MaterialSymbol {
                    text: DisplayService.internalMonitor?.name === monitorCard.monitor.name
                        ? "laptop_mac"
                        : "monitor"
                    iconSize: 24
                    color: monitorCard.selected
                        ? Appearance.colors.colOnPrimaryContainer
                        : Appearance.colors.colOnLayer1
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    StyledText {
                        Layout.fillWidth: true
                        text: monitorCard.monitor.name
                        font.pixelSize: Appearance.font.pixelSize.normal
                        font.weight: Font.Medium
                        color: monitorCard.selected
                            ? Appearance.colors.colOnPrimaryContainer
                            : Appearance.colors.colOnLayer1
                        elide: Text.ElideRight
                    }

                    StyledText {
                        Layout.fillWidth: true
                        text: monitorCard.monitor.description ?? ""
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: monitorCard.selected
                            ? Appearance.colors.colOnPrimaryContainer
                            : Appearance.colors.colOnLayer1Inactive
                        elide: Text.ElideRight
                    }

                    StyledText {
                        Layout.fillWidth: true
                        text: monitorCard.monitor.disabled
                            ? "Tidak aktif"
                            : `${monitorCard.monitor.width}×${monitorCard.monitor.height} · ${Math.round(monitorCard.monitor.refreshRate)} Hz · ${monitorCard.monitor.scale}×`
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: monitorCard.selected
                            ? Appearance.colors.colOnPrimaryContainer
                            : Appearance.colors.colOnLayer1Inactive
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: statusText.implicitWidth + 16
                    implicitHeight: 20
                    radius: height / 2
                    color: monitorCard.monitor.disabled
                        ? Appearance.colors.colLayer3
                        : Appearance.colors.colPrimary

                    StyledText {
                        id: statusText
                        anchors.centerIn: parent
                        text: monitorCard.monitor.disabled ? "Nonaktif" : "Aktif"
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        font.weight: Font.Medium
                        color: monitorCard.monitor.disabled
                            ? Appearance.colors.colOnLayer3
                            : Appearance.colors.colOnPrimary
                    }
                }
            }
        }
    }

    component WorkspaceButton: RippleButton {
        id: workspaceButton
        required property int workspaceId

        readonly property var workspaceData: HyprlandData.workspaceById[workspaceId] ?? null
        readonly property string monitorName: workspaceData?.monitor ?? ""
        readonly property bool active: HyprlandData.activeWorkspace?.id === workspaceId
        readonly property color contentColor: active
            ? Appearance.colors.colOnPrimaryContainer
            : (workspaceData ? Appearance.colors.colOnLayer2 : Appearance.colors.colOnLayer1Inactive)

        Layout.fillWidth: true
        Layout.preferredHeight: 54
        buttonRadius: Appearance.rounding.normal
        toggled: active
        colBackground: workspaceData ? Appearance.colors.colLayer2 : Appearance.colors.colLayer1
        colBackgroundHover: workspaceData ? Appearance.colors.colLayer2Hover : Appearance.colors.colLayer1Hover
        colBackgroundToggled: Appearance.colors.colPrimaryContainer
        colBackgroundToggledHover: Appearance.colors.colPrimaryContainerHover
        onClicked: DisplayService.showWorkspaceOnMonitor(workspaceId, DisplayService.workspaceTargetName)

        contentItem: Item {
            Column {
                anchors.centerIn: parent
                spacing: 0

                StyledText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: workspaceButton.workspaceId
                    font.pixelSize: Appearance.font.pixelSize.larger
                    font.weight: Font.Medium
                    color: workspaceButton.contentColor
                }

                StyledText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 70
                    text: workspaceButton.monitorName.length > 0 ? workspaceButton.monitorName : "baru"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Appearance.font.pixelSize.smallest
                    color: workspaceButton.contentColor
                    elide: Text.ElideRight
                }
            }
        }
    }
}
