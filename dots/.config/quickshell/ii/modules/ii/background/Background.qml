pragma ComponentBehavior: Bound

import qs
import qs.services
import qs.modules.common
import qs.modules.common.widgets
import qs.modules.common.widgets.widgetCanvas
import qs.modules.common.functions as CF
import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland

import qs.modules.ii.background.widgets
import qs.modules.ii.background.widgets.clock
import qs.modules.ii.background.widgets.weather

Variants {
    id: root
    model: Quickshell.screens

    PanelWindow {
        id: bgRoot

        required property var modelData

        // Hide when fullscreen
        property list<HyprlandWorkspace> workspacesForMonitor: Hyprland.workspaces.values.filter(workspace => workspace.monitor && workspace.monitor.name == monitor.name)
        property var activeWorkspaceWithFullscreen: workspacesForMonitor.filter(workspace => ((workspace.toplevels.values.filter(window => window.wayland?.fullscreen)[0] != undefined) && workspace.active))[0]
        visible: GlobalStates.screenLocked || (!(activeWorkspaceWithFullscreen != undefined)) || !Config?.options.background.hideWhenFullscreen

        // Workspaces
        property HyprlandMonitor monitor: Hyprland.monitorFor(modelData)
        property list<var> relevantWindows: HyprlandData.windowList.filter(win => win.monitor == monitor?.id && win.workspace.id >= 0).sort((a, b) => a.workspace.id - b.workspace.id)
        property int firstWorkspaceId: relevantWindows[0]?.workspace.id || 1
        property int lastWorkspaceId: relevantWindows[relevantWindows.length - 1]?.workspace.id || 10
        property int workspaceChunkSize: Config?.options.bar.workspaces.shown ?? 10
        property int totalWorkspaces: Math.ceil(lastWorkspaceId / workspaceChunkSize) * workspaceChunkSize
        // Wallpaper
        property bool wallpaperIsVideo: Config.options.background.wallpaperPath.endsWith(".mp4") || Config.options.background.wallpaperPath.endsWith(".webm") || Config.options.background.wallpaperPath.endsWith(".mkv") || Config.options.background.wallpaperPath.endsWith(".avi") || Config.options.background.wallpaperPath.endsWith(".mov")
        property string wallpaperPath: wallpaperIsVideo ? Config.options.background.thumbnailPath : Config.options.background.wallpaperPath
        readonly property string wallpaperTransition: Config.options.background.wallpaperTransition ?? "none"
        readonly property int wallpaperTransitionDuration: Config.options.background.wallpaperTransitionDuration ?? 600
        property string cachedWallpaperPath: ""
        property bool wallpaperTransitionPending: false
        property bool wallpaperTransitionRunning: false
        property real wallpaperTransitionProgress: 0
        property string wallpaperTransitionFrom: ""
        property string wallpaperTransitionTo: ""
        property string wallpaperTransitionEffect: "none"
        property string lastRandomTransitionEffect: ""
        property real wallpaperTransitionOldX: 0
        property real wallpaperTransitionOldY: 0
        property real wallpaperTransitionOldWidth: 0
        property real wallpaperTransitionOldHeight: 0
        property bool wallpaperSafetyTriggered: {
            const enabled = Config.options.workSafety.enable.wallpaper;
            const sensitiveWallpaper = (CF.StringUtils.stringListContainsSubstring(wallpaperPath.toLowerCase(), Config.options.workSafety.triggerCondition.fileKeywords));
            const sensitiveNetwork = (CF.StringUtils.stringListContainsSubstring(Network.networkName.toLowerCase(), Config.options.workSafety.triggerCondition.networkNameKeywords));
            return enabled && sensitiveWallpaper && sensitiveNetwork;
        }
        readonly property real parallaxRation: Config.options.background.parallax.workspaceZoom
        property real minSuitableScale: 1 // Some reasonable init, to be updated
        property real effectiveWallpaperScale: minSuitableScale * parallaxRation
        property int wallpaperWidth: modelData.width // Some reasonable init value, to be updated
        property int wallpaperHeight: modelData.height // Some reasonable init value, to be updated
        property real scaledWallpaperWidth: wallpaperWidth * effectiveWallpaperScale
        property real scaledWallpaperHeight: wallpaperHeight * effectiveWallpaperScale
        property real parallaxTotalPixelsX: Math.max(0, scaledWallpaperWidth - screen.width)
        property real parallaxTotalPixelsY: Math.max(0, scaledWallpaperHeight - screen.height)
        readonly property bool verticalParallax: (Config.options.background.parallax.autoVertical && wallpaperHeight > wallpaperWidth) || Config.options.background.parallax.vertical
        // Colors
        property bool shouldBlur: (GlobalStates.screenLocked && Config.options.lock.blur.enable)
        property color dominantColor: Appearance.colors.colPrimary // Default, to be changed
        property bool dominantColorIsDark: dominantColor.hslLightness < 0.5
        property color colText: {
            if (wallpaperSafetyTriggered)
                return CF.ColorUtils.mix(Appearance.colors.colOnLayer0, Appearance.colors.colPrimary, 0.75);
            return (GlobalStates.screenLocked && shouldBlur) ? Appearance.colors.colOnLayer0 : CF.ColorUtils.colorWithLightness(Appearance.colors.colPrimary, (dominantColorIsDark ? 0.8 : 0.12));
        }
        Behavior on colText {
            animation: Appearance.animation.elementMoveFast.colorAnimation.createObject(this)
        }

        // Layer props
        screen: modelData
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: (GlobalStates.screenLocked && !scaleAnim.running) ? WlrLayer.Overlay : WlrLayer.Bottom
        // WlrLayershell.layer: WlrLayer.Bottom
        WlrLayershell.namespace: "quickshell:background"
        anchors {
            top: true
            bottom: true
            left: true
            right: true
        }
        color: {
            if (!bgRoot.wallpaperSafetyTriggered || bgRoot.wallpaperIsVideo)
                return "transparent";
            return CF.ColorUtils.mix(Appearance.colors.colLayer0, Appearance.colors.colPrimary, 0.75);
        }
        Behavior on color {
            animation: Appearance.animation.elementMoveFast.colorAnimation.createObject(this)
        }

        Component.onCompleted: {
            bgRoot.cachedWallpaperPath = bgRoot.wallpaperPath;
        }

        // When transitions are disabled the cached image is not needed, so it
        // is not kept loaded (saves a full-screen image decode per change).
        onWallpaperTransitionChanged: {
            if (bgRoot.wallpaperTransition === "none")
                bgRoot.cancelWallpaperTransition();
            bgRoot.cachedWallpaperPath = bgRoot.wallpaperPath;
        }

        onWallpaperPathChanged: {
            bgRoot.updateZoomScale();
            bgRoot.handleWallpaperPathChange();
            // Clock position gets updated after zoom scale is updated
        }

        function wallpaperTransitionAllowed(from, to) {
            return bgRoot.wallpaperTransition !== "none"
                && !bgRoot.wallpaperIsVideo
                && !bgRoot.wallpaperSafetyTriggered
                && !bgRoot.shouldBlur
                && from.length > 0
                && to.length > 0
                && from !== to
                && cachedWallpaper.status === Image.Ready;
        }

        function handleWallpaperPathChange() {
            const newPath = bgRoot.wallpaperPath;

            // A second change while an animation is in progress should not
            // leave the old overlay stuck on screen. Ignore duplicate
            // notifications for the same target, otherwise cancel and let the
            // next change use the latest image.
            if (bgRoot.wallpaperTransitionPending || bgRoot.wallpaperTransitionRunning) {
                if (newPath === bgRoot.wallpaperTransitionTo)
                    return;
                bgRoot.cancelWallpaperTransition();
                bgRoot.cachedWallpaperPath = newPath;
                return;
            }

            const oldPath = bgRoot.cachedWallpaperPath;
            if (bgRoot.wallpaperTransitionAllowed(oldPath, newPath)) {
                bgRoot.beginWallpaperTransition(oldPath, newPath);
            } else if (bgRoot.wallpaperTransition !== "none") {
                bgRoot.cachedWallpaperPath = newPath;
            }
        }

        function beginWallpaperTransition(from, to) {
            bgRoot.wallpaperTransitionFrom = from;
            bgRoot.wallpaperTransitionTo = to;
            bgRoot.wallpaperTransitionProgress = 0;
            bgRoot.wallpaperTransitionEffect = bgRoot.resolveWallpaperTransitionEffect();

            // Freeze the old geometry. The base image will resize when the
            // new image's dimensions are read, but the outgoing image must
            // keep its old parallax position during the reveal.
            bgRoot.wallpaperTransitionOldX = wallpaper.x;
            bgRoot.wallpaperTransitionOldY = wallpaper.y;
            bgRoot.wallpaperTransitionOldWidth = wallpaper.width;
            bgRoot.wallpaperTransitionOldHeight = wallpaper.height;

            bgRoot.wallpaperTransitionPending = true;
            transitionReadyTimer.restart();
        }

        function resolveWallpaperTransitionEffect() {
            if (bgRoot.wallpaperTransition !== "random")
                return bgRoot.wallpaperTransition;

            // "Random" never means "none"; avoid repeating the same effect
            // twice in a row when possible.
            const effects = ["fade", "circle", "zoom", "swipe"];
            const pool = effects.filter(effect => effect !== bgRoot.lastRandomTransitionEffect);
            const chosen = pool[Math.floor(Math.random() * pool.length)];
            bgRoot.lastRandomTransitionEffect = chosen;
            return chosen;
        }

        function tryStartWallpaperTransition() {
            if (!bgRoot.wallpaperTransitionPending)
                return;

            const newImageReady = transitionNewImage.status === Image.Ready;
            const baseImageReady = wallpaper.status === Image.Ready;

            if (!newImageReady || !baseImageReady)
                return;

            bgRoot.startWallpaperTransitionAnimation();
        }

        function startWallpaperTransitionAnimation() {
            if (!bgRoot.wallpaperTransitionPending)
                return;

            bgRoot.wallpaperTransitionPending = false;
            bgRoot.wallpaperTransitionProgress = 0;
            bgRoot.wallpaperTransitionRunning = true;
            wallpaperTransitionAnimation.restart();
        }

        function forceStartWallpaperTransition() {
            if (!bgRoot.wallpaperTransitionPending)
                return;

            if (transitionNewImage.status === Image.Error) {
                bgRoot.cancelWallpaperTransition();
                bgRoot.cachedWallpaperPath = bgRoot.wallpaperPath;
                return;
            }

            bgRoot.startWallpaperTransitionAnimation();
        }

        function finishWallpaperTransition() {
            if (!bgRoot.wallpaperTransitionRunning)
                return;

            const finishedPath = bgRoot.wallpaperTransitionTo;
            bgRoot.wallpaperTransitionRunning = false;
            bgRoot.wallpaperTransitionProgress = 0;
            bgRoot.wallpaperTransitionFrom = "";
            bgRoot.wallpaperTransitionTo = "";
            bgRoot.cachedWallpaperPath = finishedPath;
        }

        function cancelWallpaperTransition() {
            transitionReadyTimer.stop();
            wallpaperTransitionAnimation.stop();
            bgRoot.wallpaperTransitionPending = false;
            bgRoot.wallpaperTransitionRunning = false;
            bgRoot.wallpaperTransitionProgress = 0;
            bgRoot.wallpaperTransitionFrom = "";
            bgRoot.wallpaperTransitionTo = "";
        }

        // Wallpaper zoom scale
        function updateZoomScale() {
            getWallpaperSizeProc.path = bgRoot.wallpaperPath;
            getWallpaperSizeProc.running = true;
        }
        Process {
            id: getWallpaperSizeProc
            property string path: bgRoot.wallpaperPath
            command: ["magick", "identify", "-format", "%w %h", path]
            stdout: StdioCollector {
                id: wallpaperSizeOutputCollector
                onStreamFinished: {
                    const output = wallpaperSizeOutputCollector.text;
                    const [width, height] = output.split(" ").map(Number);
                    const [screenWidth, screenHeight] = [bgRoot.screen.width, bgRoot.screen.height];
                    bgRoot.wallpaperWidth = width;
                    bgRoot.wallpaperHeight = height;

                    // Perfect image; scale = 1
                    // Small picture; scale > 1; will zoom in the picture
                    // Big picture; scale < 1; will zoom out the picture
                    // Choose max number so every side will fit
                    bgRoot.minSuitableScale = Math.max(screenWidth / width, screenHeight / height);
                }
            }
        }

        Item {
            anchors.fill: parent

            // Wallpaper
            StyledImage {
                id: wallpaper
                visible: opacity > 0 && !blurLoader.active
                opacity: (status === Image.Ready && !bgRoot.wallpaperIsVideo) ? 1 : 0
                cache: false
                smooth: false

                property int workspaceIndex: (bgRoot.monitor.activeWorkspace?.id ?? 1) - 1
                property real middleFraction: 0.5
                property real fraction: {
                    // 0 - start of the picture
                    // 1 - end of the picture
                    if (bgRoot.totalWorkspaces <= 1) {
                        return middleFraction;
                    }
                    return Math.max(0, Math.min(1, workspaceIndex / (bgRoot.totalWorkspaces - 1)));
                }

                property real usedFractionX: {
                    let usedFraction = middleFraction;
                    if (Config.options.background.parallax.enableWorkspace && !bgRoot.verticalParallax) {
                        usedFraction = fraction;
                    }
                    if (Config.options.background.parallax.enableSidebar) {
                        let sidebarFraction = bgRoot.parallaxRation / bgRoot.workspaceChunkSize / 2;
                        const rightPanelOpen = GlobalStates.sidebarRightOpen || GlobalStates.displayPanelOpen;
                        usedFraction += (sidebarFraction * rightPanelOpen - sidebarFraction * GlobalStates.sidebarLeftOpen);
                    }
                    return Math.max(0, Math.min(1, usedFraction));
                }
                property real usedFractionY: {
                    let usedFraction = middleFraction;
                    if (Config.options.background.parallax.enableWorkspace && bgRoot.verticalParallax) {
                        usedFraction = fraction;
                    }
                    return Math.max(0, Math.min(1, usedFraction));
                }

                x: {
                    if (bgRoot.screen.width > width) {
                        // Center the picture
                        return (bgRoot.screen.width - width) / 2;
                    }
                    return - bgRoot.parallaxTotalPixelsX * usedFractionX;
                }
                y: {
                    if (bgRoot.screen.height > height) {
                        // Center the picture
                        return (bgRoot.screen.height - height) / 2;
                    }
                    return - bgRoot.parallaxTotalPixelsY * usedFractionY;
                }

                source: bgRoot.wallpaperSafetyTriggered ? "" : bgRoot.wallpaperPath
                fillMode: Image.PreserveAspectCrop
                onStatusChanged: bgRoot.tryStartWallpaperTransition()
                Behavior on x {
                    NumberAnimation {
                        duration: 600
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on y {
                    NumberAnimation {
                        duration: 600
                        easing.type: Easing.OutCubic
                    }
                }
                width: bgRoot.scaledWallpaperWidth
                height: bgRoot.scaledWallpaperHeight
            }

            Loader {
                id: blurLoader
                active: Config.options.lock.blur.enable && (GlobalStates.screenLocked || scaleAnim.running)
                anchors.fill: wallpaper
                scale: GlobalStates.screenLocked ? Config.options.lock.blur.extraZoom : 1
                Behavior on scale {
                    NumberAnimation {
                        id: scaleAnim
                        duration: 400
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: Appearance.animationCurves.expressiveDefaultSpatial
                    }
                }
                sourceComponent: GaussianBlur {
                    source: wallpaper
                    radius: GlobalStates.screenLocked ? Config.options.lock.blur.radius : 0
                    samples: radius * 2 + 1

                    Rectangle {
                        opacity: GlobalStates.screenLocked ? 1 : 0
                        anchors.fill: parent
                        color: CF.ColorUtils.transparentize(Appearance.colors.colLayer0, 0.7)
                    }
                }
            }

            // Transition overlay. The cached image is kept loaded so a
            // wallpaper change can cover the screen immediately, even while
            // the new image is still being decoded.
            Image {
                id: cachedWallpaper
                x: bgRoot.wallpaperTransitionOldX
                y: bgRoot.wallpaperTransitionOldY
                width: bgRoot.wallpaperTransitionOldWidth > 0 ? bgRoot.wallpaperTransitionOldWidth : bgRoot.screen.width
                height: bgRoot.wallpaperTransitionOldHeight > 0 ? bgRoot.wallpaperTransitionOldHeight : bgRoot.screen.height
                source: bgRoot.wallpaperTransition === "none" ? "" : bgRoot.cachedWallpaperPath
                sourceSize: Qt.size(bgRoot.screen.width, bgRoot.screen.height)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                // Keep the cached copy loaded while it is invisible. The
                // transition needs to cover the screen immediately.
                visible: true
                opacity: (bgRoot.wallpaperTransitionPending || bgRoot.wallpaperTransitionRunning)
                    && bgRoot.wallpaperTransition !== "none"
                    && bgRoot.wallpaperTransitionFrom.length > 0
                    && !blurLoader.active ? 1 : 0
            }

            Item {
                id: transitionNewLayer
                x: wallpaper.x + (bgRoot.wallpaperTransitionEffect === "swipe"
                    ? (1 - bgRoot.wallpaperTransitionProgress) * (bgRoot.screen.width - wallpaper.x)
                    : 0)
                y: wallpaper.y
                width: wallpaper.width
                height: wallpaper.height
                visible: bgRoot.wallpaperTransitionRunning
                    && bgRoot.wallpaperTransition !== "none"
                    && !blurLoader.active
                layer.enabled: bgRoot.wallpaperTransitionEffect === "circle"
                layer.effect: OpacityMask {
                    maskSource: Item {
                        width: transitionNewLayer.width
                        height: transitionNewLayer.height
                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.max(0.01, Math.sqrt(transitionNewLayer.width * transitionNewLayer.width
                                + transitionNewLayer.height * transitionNewLayer.height) * bgRoot.wallpaperTransitionProgress)
                            height: width
                            radius: width / 2
                            color: "white"
                            antialiasing: true
                        }
                    }
                }

                Image {
                    id: transitionNewImage
                    anchors.fill: parent
                    source: bgRoot.wallpaperTransitionTo
                    sourceSize: Qt.size(bgRoot.screen.width, bgRoot.screen.height)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    opacity: bgRoot.wallpaperTransitionEffect === "fade" || bgRoot.wallpaperTransitionEffect === "zoom"
                        ? bgRoot.wallpaperTransitionProgress
                        : 1
                    scale: bgRoot.wallpaperTransitionEffect === "zoom"
                        ? 1.06 - 0.06 * bgRoot.wallpaperTransitionProgress
                        : 1
                    transformOrigin: Item.Center
                    onStatusChanged: bgRoot.tryStartWallpaperTransition()
                }
            }

            NumberAnimation {
                id: wallpaperTransitionAnimation
                target: bgRoot
                property: "wallpaperTransitionProgress"
                from: 0
                to: 1
                duration: Math.max(1, bgRoot.wallpaperTransitionDuration)
                easing.type: Easing.OutCubic
                onFinished: bgRoot.finishWallpaperTransition()
            }

            Timer {
                id: transitionReadyTimer
                interval: 1500
                repeat: false
                onTriggered: bgRoot.forceStartWallpaperTransition()
            }

            WidgetCanvas {
                id: widgetCanvas
                width: parent.width
                height: parent.height
                readonly property real parallaxFactor: {
                    var f = Config.options.background.parallax.widgetsFactor;
                    return f / bgRoot.parallaxRation;
                }
                readonly property real baseWallpaperOffsetX: (bgRoot.screen.width - wallpaper.width) / 2
                readonly property real baseWallpaperOffsetY: (bgRoot.screen.height - wallpaper.height) / 2
                readonly property real wallpaperTotalOffsetX: wallpaper.x - baseWallpaperOffsetX
                readonly property real wallpaperTotalOffsetY: wallpaper.y - baseWallpaperOffsetY
                readonly property bool locked: GlobalStates.screenLocked
                x: wallpaperTotalOffsetX * parallaxFactor * !locked
                y: wallpaperTotalOffsetY * parallaxFactor * !locked

                transitions: Transition {
                    PropertyAnimation {
                        properties: "width,height"
                        duration: Appearance.animation.elementMove.duration
                        easing.type: Appearance.animation.elementMove.type
                        easing.bezierCurve: Appearance.animation.elementMove.bezierCurve
                    }
                    AnchorAnimation {
                        duration: Appearance.animation.elementMove.duration
                        easing.type: Appearance.animation.elementMove.type
                        easing.bezierCurve: Appearance.animation.elementMove.bezierCurve
                    }
                }

                FadeLoader {
                    shown: Config.options.background.widgets.weather.enable
                    sourceComponent: WeatherWidget {
                        screenWidth: bgRoot.screen.width
                        screenHeight: bgRoot.screen.height
                        scaledScreenWidth: bgRoot.screen.width
                        scaledScreenHeight: bgRoot.screen.height
                        wallpaperScale: 1
                    }
                }

                FadeLoader {
                    shown: Config.options.background.widgets.clock.enable
                    sourceComponent: ClockWidget {
                        screenWidth: bgRoot.screen.width
                        screenHeight: bgRoot.screen.height
                        scaledScreenWidth: bgRoot.screen.width
                        scaledScreenHeight: bgRoot.screen.height
                        wallpaperScale: 1
                        wallpaperSafetyTriggered: bgRoot.wallpaperSafetyTriggered
                    }
                }
            }
        }
    }
}
