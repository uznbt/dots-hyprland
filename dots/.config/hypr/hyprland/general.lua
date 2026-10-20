-- MONITOR CONFIG
hl.monitor({
    output = "",
    mode = "preferred",
    position = "auto",
    scale = 1
})

-- Gesture 3 jari.
--
-- TANPA modifier:
--   atas / bawah -> masuk / keluar dari special workspace (scratchpad)
--   kiri / kanan -> ganti workspace. Kalau sedang di special workspace,
--                   special workspace-nya tetap terbuka di atas, jadi kamu
--                   tetap di special.
--
-- DENGAN Super ditekan:
--   atas / bawah -> jendela fokus ikut ke special workspace (atau balik lagi)
--   kiri / kanan -> jendela fokus pindah ke workspace sebelah, ikut ke sana
--
-- Untuk versi tanpa modifier dipakai gesture bawaan Hyprland ("special" dan
-- "workspace") karena sudah ada animasi workspace swipe-nya. Memindahkan
-- jendela ke workspace lain tidak ada gesture bawaannya, jadi versi Super
-- dibuat dengan fungsi Lua.

local SPECIAL_WORKSPACE = "special:special"

-- Log ke file: print() Hyprland kena filter kategori INFO, jadi tidak muncul
-- di hyprland.log.
local GESTURE_LOG = "/tmp/opencode/gesture.log"

local function logGesture(msg)
    pcall(function()
        local file = io.open(GESTURE_LOG, "a")
        if not file then
            return
        end
        file:write(tostring(msg) .. "\n")
        file:close()
    end)
end

-- Jendela yang sedang fokus, atau nil kalau tidak ada.
local function focusedWindow()
    return hl.get_active_window()
end

-- true kalau jendela fokus berada di special workspace.
local function windowInSpecialWorkspace(win)
    if not win then
        return false
    end
    local ws = win.workspace
    return ws ~= nil and ws.special == true
end

-- Special workspace di sebelahnya, kalau ada lebih dari satu.
local function adjacentSpecialWorkspace(current, step)
    local list = {}
    for _, ws in ipairs(hl.get_workspaces()) do
        if ws.special then
            list[#list + 1] = ws
        end
    end
    table.sort(list, function(a, b)
        return a.id < b.id
    end)

    for i, ws in ipairs(list) do
        if ws.id == current.id then
            return list[i + step]
        end
    end
    return nil
end

-- Pindahkan jendela fokus ke workspace tujuan, sekalian ikut pindah ke sana.
local function moveFocusedWindow(selector)
    if not focusedWindow() then
        return
    end
    hl.dsp.window.move({ workspace = selector, follow = true })
end

-- Gesture 3 jari ke atas / ke bawah: jendela ikut ke special workspace
-- (kalau sudah di special workspace, jendela balik lagi ke workspace biasa).
-- Dipakai untuk versi tanpa modifier dan versi Super, supaya tetap jalan
-- walau modifier-nya somehow nggak terdeteksi.
local function toggleWindowIntoSpecialWorkspace()
    local win = focusedWindow()
    if not win then
        logGesture("  !! tidak ada jendela fokus")
        return
    end

    local inSpecial = windowInSpecialWorkspace(win)
    local target
    if inSpecial then
        -- Keluar dari special workspace, kembali ke workspace biasa aktif.
        local normal = hl.get_active_workspace()
        target = normal and normal.name or "prev"
    else
        -- Masuk ke special workspace, jendela ikut ke sana.
        target = SPECIAL_WORKSPACE
    end

    logGesture("  -> move ke '" .. tostring(target) .. "' (dari ws '" ..
        tostring(win.workspace and win.workspace.name or "?") .. "', special=" .. tostring(inSpecial))

    moveFocusedWindow(target)
end

-- Tanpa modifier:
--   atas / bawah -> jendela ikut ke special workspace
--   kiri / kanan -> ganti workspace (dari special tetap di special)
hl.gesture({
    fingers = 3,
    direction = "vertical",
    action = function()
        logGesture("3 jari vertikal, tanpa modifier")
        toggleWindowIntoSpecialWorkspace()
        local after = focusedWindow()
        logGesture("  sesudah: win=" .. tostring(after and after.class or "NIL") ..
            " ws=" .. tostring(after and after.workspace and after.workspace.name or "NIL"))
    end
})
hl.gesture({
    fingers = 3,
    direction = "horizontal",
    action = "workspace"
})

-- Super + 3 jari ke atas / ke bawah: jendela ikut ke special workspace.
hl.gesture({
    fingers = 3,
    direction = "vertical",
    mods = "SUPER",
    action = function()
        logGesture("3 jari vertikal, dengan SUPER")
        toggleWindowIntoSpecialWorkspace()
        local after = focusedWindow()
        logGesture("  sesudah: win=" .. tostring(after and after.class or "NIL") ..
            " ws=" .. tostring(after and after.workspace and after.workspace.name or "NIL"))
    end
})

-- Diagnostik: modifier lain yang mungkin terdeteksiHyprland saat gesture
-- jalan. Cuma buat logging, tidak melakukan apa-apa.
for _, probeMods in ipairs({ "SUPER SHIFT", "CTRL", "CTRL SUPER", "ALT", "ALT SUPER", "SHIFT" }) do
    hl.gesture({
        fingers = 3,
        direction = "vertical",
        mods = probeMods,
        action = function()
            logGesture("3 jari vertikal, mods='" .. probeMods .. "'")
        end
    })
end

-- Super + 3 jari ke kiri: jendela pindah ke workspace sebelah.
-- Kalau sedang di special workspace, cari special workspace sebelah dulu;
-- kalau tidak ada, pakai workspace biasa sebelah.
hl.gesture({
    fingers = 3,
    direction = "left",
    mods = "SUPER",
    action = function()
        local win = focusedWindow()
        if not win then
            return
        end

        if windowInSpecialWorkspace(win) then
            local target = adjacentSpecialWorkspace(win.workspace, -1)
            if target then
                moveFocusedWindow(target.name)
                return
            end
        end
        moveFocusedWindow("r-1")
    end
})

-- Super + 3 jari ke kanan: jendela pindah ke workspace sebelah.
hl.gesture({
    fingers = 3,
    direction = "right",
    mods = "SUPER",
    action = function()
        local win = focusedWindow()
        if not win then
            return
        end

        if windowInSpecialWorkspace(win) then
            local target = adjacentSpecialWorkspace(win.workspace, 1)
            if target then
                moveFocusedWindow(target.name)
                return
            end
        end
        moveFocusedWindow("r+1")
    end
})

hl.gesture({
    fingers = 3,
    direction = "pinch",
    action = "fullscreen"
})
hl.gesture({
    fingers = 4,
    direction = "horizontal",
    action = "workspace"
})
hl.gesture({
    fingers = 4,
    direction = "up",
    action = function()
        hl.dispatch(hl.dsp.global("quickshell:overviewWorkspacesToggle"))
    end
})
hl.gesture({
    fingers = 4,
    direction = "down",
    action = function()
        hl.dispatch(hl.dsp.global("quickshell:overviewWorkspacesToggle"))
    end
})

hl.config({
    gestures = {
        workspace_swipe_distance = 700,
        workspace_swipe_cancel_ratio = 0.2,
        workspace_swipe_min_speed_to_force = 5,
        workspace_swipe_direction_lock = true,
        workspace_swipe_direction_lock_threshold = 10,
        workspace_swipe_create_new = true
    },
    general = {
        -- Gaps and border
        gaps_in = 4,
        gaps_out = 5,
        gaps_workspaces = 50,

        border_size = 1,

        col = {
            active_border = "rgba(0DB7D455)",
            inactive_border = "rgba(31313600)"
        },
        resize_on_border = true,

        no_focus_fallback = true,
        allow_tearing = true, -- This just allows the `immediate` window rule to work
        snap = {
            enabled = true,
            window_gap = 4,
            monitor_gap = 5,
            respect_gaps = true
        }
    },
    decoration = {
        -- 2 = circle, higher = squircle, 4 = very obvious squircle
        -- Fuck clearly visible squircles. 100% Apple brainrot.
        rounding_power = 2.5,
        rounding = 18,

        blur = {
            enabled = true,
            xray = true,
            special = false,
            new_optimizations = true,
            size = 10,
            passes = 3,
            brightness = 1,
            noise = 0.05,
            contrast = 0.89,
            vibrancy = 0.5,
            vibrancy_darkness = 0.5,
            popups = false,
            popups_ignorealpha = 0.6,
            input_methods = true,
            input_methods_ignorealpha = 0.8
        },
        shadow = {
            enabled = true,
            range = 20,
            offset = {0, 2},
            render_power = 10,
            color = "rgba(00000020)"

        },
        -- Dim
        dim_inactive = true,
        dim_strength = 0.05,
        dim_special = 0.2
    },
    animations = {
        enabled = true
    },
    dwindle = {
        preserve_split = true,
        smart_split = false,
        smart_resizing = false
        -- precise_mouse_move = true,
    },
})
-- Curves
hl.curve("expressiveFastSpatial", {
    type = "bezier",
    points = {{0.42, 1.67}, {0.21, 0.90}}
})
hl.curve("expressiveSlowSpatial", {
    type = "bezier",
    points = {{0.39, 1.29}, {0.35, 0.98}}
})
hl.curve("expressiveDefaultSpatial", {
    type = "bezier",
    points = {{0.38, 1.21}, {0.22, 1.00}}
})
hl.curve("emphasizedDecel", {
    type = "bezier",
    points = {{0.05, 0.7}, {0.1, 1}}
})
hl.curve("emphasizedAccel", {
    type = "bezier",
    points = {{0.3, 0}, {0.8, 0.15}}
})
hl.curve("standardDecel", {
    type = "bezier",
    points = {{0, 0}, {0, 1}}
})
hl.curve("menu_decel", {
    type = "bezier",
    points = {{0.1, 1}, {0, 1}}
})
hl.curve("menu_accel", {
    type = "bezier",
    points = {{0.52, 0.03}, {0.72, 0.08}}
})
hl.curve("stall", {
    type = "bezier",
    points = {{1, -0.1}, {0.7, 0.85}}
})
-- Configs
-- windows
hl.animation({
    leaf = "windowsIn",
    enabled = true,
    speed = 3,
    bezier = "emphasizedDecel",
    style = "popin 80%"
})
hl.animation({
    leaf = "fadeIn",
    enabled = true,
    speed = 3,
    bezier = "emphasizedDecel"
})
hl.animation({
    leaf = "windowsOut",
    enabled = true,
    speed = 2,
    bezier = "emphasizedDecel",
    style = "popin 90%"
})
hl.animation({
    leaf = "fadeOut",
    enabled = true,
    speed = 2,
    bezier = "emphasizedDecel"
})
hl.animation({
    leaf = "windowsMove",
    enabled = true,
    speed = 3,
    bezier = "emphasizedDecel",
    style = "slide"
})
hl.animation({
    leaf = "border",
    enabled = true,
    speed = 10,
    bezier = "emphasizedDecel"
})

-- layers
hl.animation({
    leaf = "layersIn",
    enabled = true,
    speed = 2.7,
    bezier = "emphasizedDecel",
    style = "popin 93%"
})
hl.animation({
    leaf = "layersOut",
    enabled = true,
    speed = 2.4,
    bezier = "menu_accel",
    style = "popin 94%"
})
-- fade
hl.animation({
    leaf = "fadeLayersIn",
    enabled = true,
    speed = 0.5,
    bezier = "menu_decel"
})
hl.animation({
    leaf = "fadeLayersOut",
    enabled = true,
    speed = 2.7,
    bezier = "stall"
})
-- workspaces
hl.animation({
    leaf = "workspaces",
    enabled = true,
    speed = 7,
    bezier = "menu_decel",
    style = "slide"
})
-- specialWorkspace
hl.animation({
    leaf = "specialWorkspaceIn",
    enabled = true,
    speed = 2.8,
    bezier = "emphasizedDecel",
    style = "slidevert"
})
hl.animation({
    leaf = "specialWorkspaceOut",
    enabled = true,
    speed = 1.2,
    bezier = "emphasizedAccel",
    style = "slidevert"
})
-- zoom
hl.animation({
    leaf = "zoomFactor",
    enabled = true,
    speed = 3,
    bezier = "standardDecel"
})

hl.config({
    input = {
        kb_layout = "us",
        numlock_by_default = true,
        repeat_delay = 250,
        repeat_rate = 35,

        follow_mouse = 1,
        off_window_axis_events = 2,

        touchpad = {
            natural_scroll = true,
            -- Touchpad tetap jalan While lu ngetik.
            disable_while_typing = false,
            clickfinger_behavior = true,
            scroll_factor = 0.7
        }
    },

    misc = {
        disable_hyprland_logo = true,
        disable_splash_rendering = true,
        vrr = 0,
        mouse_move_enables_dpms = true,
        key_press_enables_dpms = true,
        animate_manual_resizes = false,
        animate_mouse_windowdragging = false,
        enable_swallow = false,
        swallow_regex = "(foot|kitty|allacritty|Alacritty)",
        on_focus_under_fullscreen = 2,
        allow_session_lock_restore = true,
        session_lock_xray = true,
        initial_workspace_tracking = false,
        focus_on_activate = true
    },

    binds = {
        scroll_event_delay = 0,
        hide_special_on_workspace_change = true
    },

    cursor = {
        zoom_factor = 1,
        zoom_rigid = false,
        zoom_disable_aa = true,
        hotspot_padding = 1
    },

    xwayland = {
        force_zero_scaling = true
    }
})
