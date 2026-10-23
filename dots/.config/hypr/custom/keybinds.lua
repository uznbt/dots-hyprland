-- This file will not be overwritten across dots-hyprland updates.
-- The file name is for the sake of organization and does not matter
-- See the corresponding files in ~/.config/hypr/hyprland for examples

-- Override SUPER+Tab: remove the default Overview binding and replace with next workspace
hl.unbind("SUPER + Tab")
hl.bind("SUPER + Tab",         hl.dsp.focus({ workspace = "r+1" }), { description = "Workspace: Next" })
hl.bind("SUPER + SHIFT + Tab", hl.dsp.focus({ workspace = "r-1" }), { description = "Workspace: Previous" })

-- Alt+Tab / Alt+Shift+Tab: cycle window focus using exec_raw (native Lua dispatcher)
hl.bind("ALT + Tab",         hl.dsp.exec_raw("cyclenext"),      { description = "Window: Focus next" })
hl.bind("ALT + SHIFT + Tab", hl.dsp.exec_raw("cyclenext prev"), { description = "Window: Focus previous" })
