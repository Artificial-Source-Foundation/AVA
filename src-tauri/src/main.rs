// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    // Work around WebKitGTK DMABUF renderer ghosting on Wayland compositors
    // (COSMIC, Hyprland, Sway, etc.) with NVIDIA drivers.
    // The DMABUF renderer produces ghost/shadow copies of DOM elements when
    // the GBM driver fails to load. Must be set before WebKitGTK initializes.
    // See: https://github.com/tauri-apps/tauri/issues/13157
    #[cfg(target_os = "linux")]
    if std::env::var("WEBKIT_DISABLE_DMABUF_RENDERER").is_err() {
        std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    }

    estela_lib::run()
}
