import QtQuick
import Quickshell

import qs.modules.common

/**
 * LazyLoader for panel family members.
 *
 * A `lazy` panel is not instantiated until it is opened for the first time
 * (`open` becomes true). Once opened it stays loaded (`everLoaded`), so
 * closing and reopening is instant and in-flight animations are not cut off.
 *
 * The panel file is still compiled with the family at startup; only its
 * objects (windows, timers, connections) are created on demand.
 */
LazyLoader {
    id: loader

    property bool extraCondition: true
    property bool lazy: false
    property bool open: false
    property bool everLoaded: false

    active: Config.ready && extraCondition && (!lazy || open || everLoaded)
    onActiveChanged: if (active) everLoaded = true
}
