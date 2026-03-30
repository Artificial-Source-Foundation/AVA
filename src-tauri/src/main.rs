// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    // Work around WebKitGTK DMABUF renderer ghosting on NVIDIA + Wayland.
    // The DMABUF renderer produces ghost/shadow copies of DOM elements when
    // NVIDIA's GBM driver fails DMA-BUF format/modifier negotiation.
    // Only applied when actually on NVIDIA + Wayland so AMD/Intel users get
    // full GPU acceleration.
    // See: https://github.com/tauri-apps/tauri/issues/13157
    //      https://bugs.webkit.org/show_bug.cgi?id=262607
    #[cfg(target_os = "linux")]
    {
        let is_wayland = std::env::var("WAYLAND_DISPLAY").is_ok()
            || std::env::var("XDG_SESSION_TYPE")
                .map(|v| v == "wayland")
                .unwrap_or(false);
        let is_nvidia = std::path::Path::new("/proc/driver/nvidia").exists();

        if is_wayland && is_nvidia {
            if std::env::var("WEBKIT_DISABLE_DMABUF_RENDERER").is_err() {
                std::env::set_var("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
            }
            // Disable explicit sync — reduces frame stuttering on NVIDIA Wayland
            if std::env::var("__NV_DISABLE_EXPLICIT_SYNC").is_err() {
                std::env::set_var("__NV_DISABLE_EXPLICIT_SYNC", "1");
            }
        }
    }

    ava_lib::run()
}
