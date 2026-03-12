use ratatui::style::Color;
use serde::Deserialize;
use std::path::{Path, PathBuf};
use std::sync::OnceLock;

/// Cached list of custom themes loaded from ~/.ava/themes/*.toml.
static CUSTOM_THEMES: OnceLock<Vec<Theme>> = OnceLock::new();

/// Built-in theme names (ordering matters for cycle).
/// Dark themes first, then light themes.
const BUILTIN_NAMES: &[&str] = &[
    // Dark themes
    "default",
    "dracula",
    "nord",
    "gruvbox",
    "catppuccin",
    "solarized_dark",
    "tokyo_night",
    "one_dark",
    "rose_pine",
    "kanagawa",
    "monokai",
    "material",
    "ayu_dark",
    "ayu_mirage",
    "everforest",
    "nightfox",
    "github_dark",
    "moonlight",
    "synthwave",
    "palenight",
    "onedark_vivid",
    "horizon",
    "poimandres",
    "vesper",
    // Light themes
    "github_light",
    "solarized_light",
    "catppuccin_latte",
    "one_light",
    "rose_pine_dawn",
];

#[derive(Debug, Clone)]
pub struct Theme {
    pub name: String,
    // Core palette
    pub primary: Color,
    pub secondary: Color,
    pub accent: Color,
    pub success: Color,
    pub error: Color,
    pub warning: Color,
    // Text hierarchy
    pub text: Color,
    pub text_muted: Color,
    pub text_dimmed: Color,
    // Surfaces
    pub bg: Color,
    pub bg_elevated: Color,
    pub bg_surface: Color,
    pub bg_deep: Color,
    pub bg_user_message: Color,
    // Borders
    pub border: Color,
    pub border_active: Color,
    pub border_subtle: Color,
    // Diff
    pub diff_added: Color,
    pub diff_removed: Color,
    pub diff_context: Color,
    pub diff_hunk_header: Color,
    pub diff_added_highlight: Color,
    pub diff_removed_highlight: Color,
    pub diff_added_bg: Color,
    pub diff_removed_bg: Color,
    // Risk levels (for tool approval)
    pub risk_safe: Color,
    pub risk_low: Color,
    pub risk_medium: Color,
    pub risk_high: Color,
    pub risk_critical: Color,
}

// ---------------------------------------------------------------------------
// TOML deserialization helpers
// ---------------------------------------------------------------------------

/// Raw TOML representation of a theme file.
/// All color fields are optional — missing fields fall back to the default theme.
#[derive(Debug, Deserialize)]
struct ThemeFile {
    name: String,
    #[serde(default)]
    colors: ThemeColors,
}

/// Every field is optional so users only need to specify what they want to override.
#[derive(Debug, Default, Deserialize)]
#[serde(default)]
struct ThemeColors {
    // Core palette
    primary: Option<String>,
    secondary: Option<String>,
    accent: Option<String>,
    success: Option<String>,
    error: Option<String>,
    warning: Option<String>,
    // Text hierarchy
    text: Option<String>,
    text_muted: Option<String>,
    text_dimmed: Option<String>,
    // Surfaces
    bg: Option<String>,
    bg_elevated: Option<String>,
    bg_surface: Option<String>,
    bg_deep: Option<String>,
    bg_user_message: Option<String>,
    // Borders
    border: Option<String>,
    border_active: Option<String>,
    border_subtle: Option<String>,
    // Diff
    diff_added: Option<String>,
    diff_removed: Option<String>,
    diff_context: Option<String>,
    diff_hunk_header: Option<String>,
    diff_added_highlight: Option<String>,
    diff_removed_highlight: Option<String>,
    diff_added_bg: Option<String>,
    diff_removed_bg: Option<String>,
    // Risk
    risk_safe: Option<String>,
    risk_low: Option<String>,
    risk_medium: Option<String>,
    risk_high: Option<String>,
    risk_critical: Option<String>,
}

/// Parse a hex color string like "#4D9EF6" or "4D9EF6" into a `Color::Rgb`.
/// Returns `None` for invalid input.
fn parse_hex_color(s: &str) -> Option<Color> {
    let hex = s.strip_prefix('#').unwrap_or(s);
    if hex.len() != 6 {
        return None;
    }
    let r = u8::from_str_radix(&hex[0..2], 16).ok()?;
    let g = u8::from_str_radix(&hex[2..4], 16).ok()?;
    let b = u8::from_str_radix(&hex[4..6], 16).ok()?;
    Some(Color::Rgb(r, g, b))
}

/// Resolve an optional hex string to a `Color`, falling back to the given default.
fn resolve_color(hex: &Option<String>, fallback: Color) -> Color {
    match hex {
        Some(s) => parse_hex_color(s).unwrap_or(fallback),
        None => fallback,
    }
}

impl Theme {
    pub fn default_theme() -> Self {
        Self {
            name: "default".into(),
            // Pencil design system palette
            primary: Color::Rgb(77, 158, 246), // #4D9EF6 — accent-primary
            secondary: Color::Rgb(123, 97, 255), // #7B61FF — accent-secondary (purple)
            accent: Color::Rgb(251, 191, 36),  // #FBBF24 — accent-warning
            success: Color::Rgb(52, 211, 153), // #34D399 — accent-success
            error: Color::Rgb(248, 113, 113),  // #F87171 — accent-error
            warning: Color::Rgb(251, 191, 36), // #FBBF24 — accent-warning
            // Text
            text: Color::Rgb(232, 236, 241), // #E8ECF1 — text-primary
            text_muted: Color::Rgb(139, 149, 165), // #8B95A5 — text-secondary
            text_dimmed: Color::Rgb(95, 106, 125), // #5F6A7D — readable on #0B0E14 bg
            // Surfaces
            bg: Color::Rgb(11, 14, 20),              // #0B0E14 — bg-deep
            bg_elevated: Color::Rgb(26, 31, 46),     // #1A1F2E — bg-elevated
            bg_surface: Color::Rgb(19, 23, 32),      // #131720 — bg-surface
            bg_deep: Color::Rgb(11, 14, 20),         // #0B0E14 — bg-deep
            bg_user_message: Color::Rgb(26, 31, 46), // #1A1F2E — bg-elevated
            // Borders
            border: Color::Rgb(42, 49, 66), // #2A3142 — visible on bg and bg_surface
            border_active: Color::Rgb(77, 158, 246), // #4D9EF6 — accent-primary
            border_subtle: Color::Rgb(30, 36, 51), // #1E2433 — subtle but distinct from border
            // Diff
            diff_added: Color::Rgb(63, 185, 80),        // #3fb950
            diff_removed: Color::Rgb(248, 81, 73),      // #f85149
            diff_context: Color::Rgb(125, 133, 144),    // #7d8590
            diff_hunk_header: Color::Rgb(88, 166, 255), // #58a6ff
            diff_added_highlight: Color::Rgb(70, 220, 100), // brighter green
            diff_removed_highlight: Color::Rgb(255, 120, 110), // brighter red
            diff_added_bg: Color::Rgb(26, 71, 33),      // #1a4721
            diff_removed_bg: Color::Rgb(103, 6, 12),    // #67060c
            // Risk
            risk_safe: Color::Rgb(63, 185, 80),     // success
            risk_low: Color::Rgb(88, 166, 255),     // primary
            risk_medium: Color::Rgb(210, 153, 34),  // warning
            risk_high: Color::Rgb(248, 81, 73),     // error
            risk_critical: Color::Rgb(255, 60, 50), // bright red
        }
    }

    pub fn dracula() -> Self {
        Self {
            name: "dracula".into(),
            primary: Color::Rgb(139, 233, 253),     // cyan
            secondary: Color::Rgb(80, 250, 123),    // green
            accent: Color::Rgb(255, 184, 108),      // orange
            success: Color::Rgb(80, 250, 123),      // green
            error: Color::Rgb(255, 85, 85),         // red
            warning: Color::Rgb(241, 250, 140),     // yellow
            text: Color::Rgb(248, 248, 242),        // foreground
            text_muted: Color::Rgb(130, 145, 190),  // brighter comment — readable on all bgs
            text_dimmed: Color::Rgb(100, 105, 128), // readable on bg(40,42,54) and bg_elevated
            bg: Color::Rgb(40, 42, 54),             // background
            bg_elevated: Color::Rgb(50, 52, 66),    // slightly lighter
            bg_surface: Color::Rgb(60, 62, 76),     // header/footer bars
            bg_deep: Color::Rgb(35, 37, 48),        // input fields, code blocks
            bg_user_message: Color::Rgb(50, 52, 66),
            border: Color::Rgb(75, 78, 98), // visible on bg, distinct from bg_surface
            border_active: Color::Rgb(139, 233, 253), // cyan
            border_subtle: Color::Rgb(62, 65, 82), // subtle but visible on bg(40,42,54)
            diff_added: Color::Rgb(80, 250, 123),
            diff_removed: Color::Rgb(255, 85, 85),
            diff_context: Color::Rgb(130, 145, 190), // matches text_muted for readability
            diff_hunk_header: Color::Rgb(139, 233, 253),
            diff_added_highlight: Color::Rgb(120, 255, 160),
            diff_removed_highlight: Color::Rgb(255, 120, 120),
            diff_added_bg: Color::Rgb(30, 60, 40),
            diff_removed_bg: Color::Rgb(70, 30, 30),
            risk_safe: Color::Rgb(80, 250, 123),
            risk_low: Color::Rgb(139, 233, 253),
            risk_medium: Color::Rgb(241, 250, 140),
            risk_high: Color::Rgb(255, 85, 85),
            risk_critical: Color::Rgb(255, 50, 50),
        }
    }

    pub fn nord() -> Self {
        Self {
            name: "nord".into(),
            primary: Color::Rgb(136, 192, 208),    // nord8 frost
            secondary: Color::Rgb(129, 161, 193),  // nord9
            accent: Color::Rgb(235, 203, 139),     // nord13 yellow
            success: Color::Rgb(163, 190, 140),    // nord14 green
            error: Color::Rgb(191, 97, 106),       // nord11 red
            warning: Color::Rgb(208, 135, 112),    // nord12 orange
            text: Color::Rgb(236, 239, 244),       // nord6 snow
            text_muted: Color::Rgb(120, 132, 156), // brighter nord3 — readable on nord0/nord2
            text_dimmed: Color::Rgb(95, 106, 128), // readable on bg(46,52,64) and bg_elevated
            bg: Color::Rgb(46, 52, 64),            // nord0 polar night
            bg_elevated: Color::Rgb(59, 66, 82),   // nord2
            bg_surface: Color::Rgb(67, 76, 94),    // nord3 — header/footer bars
            bg_deep: Color::Rgb(41, 46, 56),       // darker than bg
            bg_user_message: Color::Rgb(59, 66, 82),
            border: Color::Rgb(80, 90, 110), // brighter than bg_surface, visible everywhere
            border_active: Color::Rgb(136, 192, 208), // nord8
            border_subtle: Color::Rgb(67, 76, 94), // nord3 — visible on bg, subtle on surface
            diff_added: Color::Rgb(163, 190, 140), // nord14
            diff_removed: Color::Rgb(191, 97, 106), // nord11
            diff_context: Color::Rgb(129, 161, 193),
            diff_hunk_header: Color::Rgb(136, 192, 208),
            diff_added_highlight: Color::Rgb(180, 210, 160),
            diff_removed_highlight: Color::Rgb(210, 120, 130),
            diff_added_bg: Color::Rgb(55, 70, 55),
            diff_removed_bg: Color::Rgb(70, 50, 55),
            risk_safe: Color::Rgb(163, 190, 140),
            risk_low: Color::Rgb(136, 192, 208),
            risk_medium: Color::Rgb(235, 203, 139),
            risk_high: Color::Rgb(191, 97, 106),
            risk_critical: Color::Rgb(200, 80, 90),
        }
    }

    pub fn gruvbox() -> Self {
        Self {
            name: "gruvbox".into(),
            // Gruvbox Dark — warm retro palette by morhetz
            primary: Color::Rgb(69, 133, 136),     // #458588 — aqua
            secondary: Color::Rgb(177, 98, 134),   // #B16286 — purple
            accent: Color::Rgb(214, 153, 62),      // #D69E3E — bright yellow-orange
            success: Color::Rgb(152, 151, 26),     // #98971A — green
            error: Color::Rgb(204, 36, 29),        // #CC241D — red
            warning: Color::Rgb(215, 153, 33),     // #D79921 — yellow
            text: Color::Rgb(235, 219, 178),       // #EBDBB2 — fg
            text_muted: Color::Rgb(168, 153, 132), // #A89984 — gray
            text_dimmed: Color::Rgb(124, 111, 100), // #7C6F64 — bg4
            bg: Color::Rgb(40, 40, 40),            // #282828 — bg
            bg_elevated: Color::Rgb(60, 56, 54),   // #3C3836 — bg1
            bg_surface: Color::Rgb(80, 73, 69),    // #504945 — bg2
            bg_deep: Color::Rgb(29, 32, 33),       // #1D2021 — bg0_h (hard)
            bg_user_message: Color::Rgb(60, 56, 54), // #3C3836 — bg1
            border: Color::Rgb(80, 73, 69),        // #504945 — bg2
            border_active: Color::Rgb(69, 133, 136), // #458588 — aqua
            border_subtle: Color::Rgb(60, 56, 54), // #3C3836 — bg1
            diff_added: Color::Rgb(184, 187, 38),  // #B8BB26 — bright green
            diff_removed: Color::Rgb(251, 73, 52), // #FB4934 — bright red
            diff_context: Color::Rgb(168, 153, 132), // #A89984 — gray
            diff_hunk_header: Color::Rgb(131, 165, 152), // #83A598 — bright aqua
            diff_added_highlight: Color::Rgb(202, 210, 70),
            diff_removed_highlight: Color::Rgb(255, 110, 90),
            diff_added_bg: Color::Rgb(50, 60, 30),
            diff_removed_bg: Color::Rgb(80, 30, 25),
            risk_safe: Color::Rgb(184, 187, 38),   // bright green
            risk_low: Color::Rgb(131, 165, 152),   // bright aqua
            risk_medium: Color::Rgb(250, 189, 47), // #FABD2F — bright yellow
            risk_high: Color::Rgb(251, 73, 52),    // bright red
            risk_critical: Color::Rgb(255, 50, 40),
        }
    }

    pub fn catppuccin() -> Self {
        Self {
            name: "catppuccin".into(),
            // Catppuccin Mocha — pastel dark variant
            primary: Color::Rgb(137, 180, 250),   // #89B4FA — blue
            secondary: Color::Rgb(203, 166, 247), // #CBA6F7 — mauve
            accent: Color::Rgb(249, 226, 175),    // #F9E2AF — yellow
            success: Color::Rgb(166, 227, 161),   // #A6E3A1 — green
            error: Color::Rgb(243, 139, 168),     // #F38BA8 — red
            warning: Color::Rgb(250, 179, 135),   // #FAB387 — peach
            text: Color::Rgb(205, 214, 244),      // #CDD6F4 — text
            text_muted: Color::Rgb(162, 169, 206), // #A2A9CE — subtext0 approx
            text_dimmed: Color::Rgb(108, 112, 134), // #6C7086 — overlay0
            bg: Color::Rgb(30, 30, 46),           // #1E1E2E — base
            bg_elevated: Color::Rgb(49, 50, 68),  // #313244 — surface0
            bg_surface: Color::Rgb(69, 71, 90),   // #45475A — surface1
            bg_deep: Color::Rgb(24, 24, 37),      // #181825 — mantle
            bg_user_message: Color::Rgb(49, 50, 68), // #313244 — surface0
            border: Color::Rgb(69, 71, 90),       // #45475A — surface1
            border_active: Color::Rgb(137, 180, 250), // #89B4FA — blue
            border_subtle: Color::Rgb(49, 50, 68), // #313244 — surface0
            diff_added: Color::Rgb(166, 227, 161), // green
            diff_removed: Color::Rgb(243, 139, 168), // red
            diff_context: Color::Rgb(162, 169, 206),
            diff_hunk_header: Color::Rgb(137, 180, 250),
            diff_added_highlight: Color::Rgb(190, 240, 185),
            diff_removed_highlight: Color::Rgb(255, 170, 190),
            diff_added_bg: Color::Rgb(35, 55, 40),
            diff_removed_bg: Color::Rgb(65, 35, 45),
            risk_safe: Color::Rgb(166, 227, 161),
            risk_low: Color::Rgb(137, 180, 250),
            risk_medium: Color::Rgb(249, 226, 175),
            risk_high: Color::Rgb(243, 139, 168),
            risk_critical: Color::Rgb(255, 100, 120),
        }
    }

    pub fn solarized_dark() -> Self {
        Self {
            name: "solarized_dark".into(),
            // Solarized Dark — Ethan Schoonover's precision color scheme
            primary: Color::Rgb(38, 139, 210),    // #268BD2 — blue
            secondary: Color::Rgb(108, 113, 196), // #6C71C4 — violet
            accent: Color::Rgb(181, 137, 0),      // #B58900 — yellow
            success: Color::Rgb(133, 153, 0),     // #859900 — green
            error: Color::Rgb(220, 50, 47),       // #DC322F — red
            warning: Color::Rgb(203, 75, 22),     // #CB4B16 — orange
            text: Color::Rgb(131, 148, 150),      // #839496 — base0 (main body)
            text_muted: Color::Rgb(88, 110, 117), // #586E75 — base01
            text_dimmed: Color::Rgb(73, 80, 87),  // #494F57 — between base01/base02
            bg: Color::Rgb(0, 43, 54),            // #002B36 — base03
            bg_elevated: Color::Rgb(7, 54, 66),   // #073642 — base02
            bg_surface: Color::Rgb(22, 70, 82),   // slightly lighter
            bg_deep: Color::Rgb(0, 34, 43),       // darker than base03
            bg_user_message: Color::Rgb(7, 54, 66), // base02
            border: Color::Rgb(7, 54, 66),        // base02
            border_active: Color::Rgb(38, 139, 210), // blue
            border_subtle: Color::Rgb(0, 50, 62), // between base02/base03
            diff_added: Color::Rgb(133, 153, 0),  // green
            diff_removed: Color::Rgb(220, 50, 47), // red
            diff_context: Color::Rgb(88, 110, 117), // base01
            diff_hunk_header: Color::Rgb(38, 139, 210), // blue
            diff_added_highlight: Color::Rgb(160, 180, 40),
            diff_removed_highlight: Color::Rgb(245, 80, 75),
            diff_added_bg: Color::Rgb(10, 55, 30),
            diff_removed_bg: Color::Rgb(60, 20, 20),
            risk_safe: Color::Rgb(133, 153, 0),
            risk_low: Color::Rgb(38, 139, 210),
            risk_medium: Color::Rgb(181, 137, 0),
            risk_high: Color::Rgb(220, 50, 47),
            risk_critical: Color::Rgb(240, 40, 40),
        }
    }

    pub fn tokyo_night() -> Self {
        Self {
            name: "tokyo_night".into(),
            // Tokyo Night — inspired by Tokyo city lights
            primary: Color::Rgb(122, 162, 247),   // #7AA2F7 — blue
            secondary: Color::Rgb(187, 154, 247), // #BB9AF7 — purple
            accent: Color::Rgb(224, 175, 104),    // #E0AF68 — yellow
            success: Color::Rgb(115, 218, 202),   // #73DACA — teal/green
            error: Color::Rgb(247, 118, 142),     // #F7768E — red
            warning: Color::Rgb(255, 158, 100),   // #FF9E64 — orange
            text: Color::Rgb(192, 202, 245),      // #C0CAF5 — foreground
            text_muted: Color::Rgb(86, 95, 137),  // #565F89 — comment
            text_dimmed: Color::Rgb(59, 66, 97),  // #3B4261 — dark comment
            bg: Color::Rgb(26, 27, 38),           // #1A1B26 — background
            bg_elevated: Color::Rgb(36, 40, 59),  // #24283B — bg_highlight
            bg_surface: Color::Rgb(52, 56, 78),   // #34384E — lighter surface
            bg_deep: Color::Rgb(22, 22, 30),      // #16161E — bg_dark
            bg_user_message: Color::Rgb(36, 40, 59), // bg_highlight
            border: Color::Rgb(41, 46, 66),       // #292E42 — border
            border_active: Color::Rgb(122, 162, 247), // blue
            border_subtle: Color::Rgb(33, 36, 52), // #212434
            diff_added: Color::Rgb(115, 218, 202), // teal
            diff_removed: Color::Rgb(247, 118, 142), // red
            diff_context: Color::Rgb(86, 95, 137), // comment
            diff_hunk_header: Color::Rgb(122, 162, 247), // blue
            diff_added_highlight: Color::Rgb(145, 235, 220),
            diff_removed_highlight: Color::Rgb(255, 150, 165),
            diff_added_bg: Color::Rgb(25, 50, 45),
            diff_removed_bg: Color::Rgb(60, 30, 35),
            risk_safe: Color::Rgb(115, 218, 202),
            risk_low: Color::Rgb(122, 162, 247),
            risk_medium: Color::Rgb(224, 175, 104),
            risk_high: Color::Rgb(247, 118, 142),
            risk_critical: Color::Rgb(255, 80, 100),
        }
    }

    pub fn one_dark() -> Self {
        Self {
            name: "one_dark".into(),
            // One Dark — Atom's classic dark theme
            primary: Color::Rgb(97, 175, 239),    // #61AFEF — blue
            secondary: Color::Rgb(198, 120, 221), // #C678DD — purple
            accent: Color::Rgb(229, 192, 123),    // #E5C07B — yellow
            success: Color::Rgb(152, 195, 121),   // #98C379 — green
            error: Color::Rgb(224, 108, 117),     // #E06C75 — red
            warning: Color::Rgb(209, 154, 102),   // #D19A66 — orange
            text: Color::Rgb(171, 178, 191),      // #ABB2BF — foreground
            text_muted: Color::Rgb(92, 99, 112),  // #5C6370 — comment
            text_dimmed: Color::Rgb(63, 68, 80),  // #3F4450 — gutter
            bg: Color::Rgb(40, 44, 52),           // #282C34 — background
            bg_elevated: Color::Rgb(50, 56, 66),  // #323842 — lighter
            bg_surface: Color::Rgb(62, 68, 81),   // #3E4451 — selection
            bg_deep: Color::Rgb(33, 37, 43),      // #21252B — darker
            bg_user_message: Color::Rgb(50, 56, 66), // lighter bg
            border: Color::Rgb(62, 68, 81),       // #3E4451 — gutter/selection
            border_active: Color::Rgb(97, 175, 239), // blue
            border_subtle: Color::Rgb(46, 50, 60), // between bg and surface
            diff_added: Color::Rgb(152, 195, 121), // green
            diff_removed: Color::Rgb(224, 108, 117), // red
            diff_context: Color::Rgb(92, 99, 112),
            diff_hunk_header: Color::Rgb(97, 175, 239),
            diff_added_highlight: Color::Rgb(175, 215, 145),
            diff_removed_highlight: Color::Rgb(240, 140, 145),
            diff_added_bg: Color::Rgb(40, 60, 35),
            diff_removed_bg: Color::Rgb(65, 35, 35),
            risk_safe: Color::Rgb(152, 195, 121),
            risk_low: Color::Rgb(97, 175, 239),
            risk_medium: Color::Rgb(229, 192, 123),
            risk_high: Color::Rgb(224, 108, 117),
            risk_critical: Color::Rgb(240, 80, 90),
        }
    }

    pub fn rose_pine() -> Self {
        Self {
            name: "rose_pine".into(),
            // Rose Pine — elegant dark with rose accents
            primary: Color::Rgb(156, 207, 216),   // #9CCFD8 — foam
            secondary: Color::Rgb(196, 167, 231), // #C4A7E7 — iris
            accent: Color::Rgb(234, 154, 151),    // #EA9A97 — rose
            success: Color::Rgb(62, 143, 176),    // #3E8FB0 — pine
            error: Color::Rgb(235, 111, 146),     // #EB6F92 — love
            warning: Color::Rgb(246, 193, 119),   // #F6C177 — gold
            text: Color::Rgb(224, 222, 244),      // #E0DEF4 — text
            text_muted: Color::Rgb(144, 140, 170), // #908CAA — subtle
            text_dimmed: Color::Rgb(110, 106, 134), // #6E6A86 — muted
            bg: Color::Rgb(25, 23, 36),           // #191724 — base
            bg_elevated: Color::Rgb(38, 35, 58),  // #26233A — overlay
            bg_surface: Color::Rgb(31, 29, 46),   // #1F1D2E — surface
            bg_deep: Color::Rgb(20, 18, 29),      // darker than base
            bg_user_message: Color::Rgb(38, 35, 58), // overlay
            border: Color::Rgb(38, 35, 58),       // overlay
            border_active: Color::Rgb(156, 207, 216), // foam
            border_subtle: Color::Rgb(31, 29, 46), // surface
            diff_added: Color::Rgb(62, 143, 176), // pine
            diff_removed: Color::Rgb(235, 111, 146), // love
            diff_context: Color::Rgb(144, 140, 170), // subtle
            diff_hunk_header: Color::Rgb(156, 207, 216), // foam
            diff_added_highlight: Color::Rgb(90, 170, 200),
            diff_removed_highlight: Color::Rgb(255, 140, 170),
            diff_added_bg: Color::Rgb(25, 45, 50),
            diff_removed_bg: Color::Rgb(55, 25, 35),
            risk_safe: Color::Rgb(62, 143, 176),
            risk_low: Color::Rgb(156, 207, 216),
            risk_medium: Color::Rgb(246, 193, 119),
            risk_high: Color::Rgb(235, 111, 146),
            risk_critical: Color::Rgb(255, 80, 110),
        }
    }

    pub fn kanagawa() -> Self {
        Self {
            name: "kanagawa".into(),
            // Kanagawa — inspired by Katsushika Hokusai's "The Great Wave"
            primary: Color::Rgb(126, 156, 216), // #7E9CD8 — crystal blue
            secondary: Color::Rgb(149, 127, 184), // #957FB8 — oni violet
            accent: Color::Rgb(255, 169, 104),  // #FFA066 — surimi orange
            success: Color::Rgb(118, 169, 111), // #76A96F — autumn green
            error: Color::Rgb(195, 64, 67),     // #C34043 — autumn red
            warning: Color::Rgb(220, 165, 97),  // #DCA561 — boat yellow
            text: Color::Rgb(220, 215, 186),    // #DCD7BA — fuji white
            text_muted: Color::Rgb(114, 113, 105), // #727169 — fuji gray
            text_dimmed: Color::Rgb(84, 84, 88), // #545458 — sumi ink 4
            bg: Color::Rgb(31, 31, 40),         // #1F1F28 — sumi ink 1
            bg_elevated: Color::Rgb(42, 42, 54), // #2A2A36 — sumi ink 2 approx
            bg_surface: Color::Rgb(54, 54, 70), // #363646 — sumi ink 3
            bg_deep: Color::Rgb(22, 22, 28),    // #16161D — sumi ink 0
            bg_user_message: Color::Rgb(42, 42, 54), // sumi ink 2
            border: Color::Rgb(54, 54, 70),     // sumi ink 3
            border_active: Color::Rgb(126, 156, 216), // crystal blue
            border_subtle: Color::Rgb(42, 42, 54), // sumi ink 2
            diff_added: Color::Rgb(118, 169, 111), // autumn green
            diff_removed: Color::Rgb(195, 64, 67), // autumn red
            diff_context: Color::Rgb(114, 113, 105), // fuji gray
            diff_hunk_header: Color::Rgb(126, 156, 216), // crystal blue
            diff_added_highlight: Color::Rgb(145, 195, 140),
            diff_removed_highlight: Color::Rgb(225, 95, 100),
            diff_added_bg: Color::Rgb(35, 50, 35),
            diff_removed_bg: Color::Rgb(60, 25, 25),
            risk_safe: Color::Rgb(118, 169, 111),
            risk_low: Color::Rgb(126, 156, 216),
            risk_medium: Color::Rgb(220, 165, 97),
            risk_high: Color::Rgb(195, 64, 67),
            risk_critical: Color::Rgb(230, 50, 50),
        }
    }

    pub fn monokai() -> Self {
        Self {
            name: "monokai".into(),
            // Monokai — classic Sublime Text dark theme (Monokai Pro)
            primary: Color::Rgb(102, 217, 239), // #66D9EF — blue/cyan
            secondary: Color::Rgb(174, 129, 255), // #AE81FF — purple
            accent: Color::Rgb(230, 219, 116),  // #E6DB74 — yellow
            success: Color::Rgb(166, 226, 46),  // #A6E22E — green
            error: Color::Rgb(249, 38, 114),    // #F92672 — red/magenta
            warning: Color::Rgb(253, 151, 31),  // #FD971F — orange
            text: Color::Rgb(248, 248, 242),    // #F8F8F2 — foreground
            text_muted: Color::Rgb(117, 113, 94), // #75715E — comment
            text_dimmed: Color::Rgb(90, 86, 73), // muted comment
            bg: Color::Rgb(39, 40, 34),         // #272822 — background
            bg_elevated: Color::Rgb(52, 53, 46), // slightly lighter
            bg_surface: Color::Rgb(65, 66, 58), // surface
            bg_deep: Color::Rgb(30, 31, 26),    // darker
            bg_user_message: Color::Rgb(52, 53, 46),
            border: Color::Rgb(70, 71, 62),           // visible border
            border_active: Color::Rgb(102, 217, 239), // cyan
            border_subtle: Color::Rgb(52, 53, 46),
            diff_added: Color::Rgb(166, 226, 46),   // green
            diff_removed: Color::Rgb(249, 38, 114), // red
            diff_context: Color::Rgb(117, 113, 94),
            diff_hunk_header: Color::Rgb(102, 217, 239),
            diff_added_highlight: Color::Rgb(190, 240, 80),
            diff_removed_highlight: Color::Rgb(255, 80, 140),
            diff_added_bg: Color::Rgb(40, 55, 25),
            diff_removed_bg: Color::Rgb(65, 20, 35),
            risk_safe: Color::Rgb(166, 226, 46),
            risk_low: Color::Rgb(102, 217, 239),
            risk_medium: Color::Rgb(230, 219, 116),
            risk_high: Color::Rgb(249, 38, 114),
            risk_critical: Color::Rgb(255, 20, 80),
        }
    }

    pub fn material() -> Self {
        Self {
            name: "material".into(),
            // Material Design dark — based on material-theme
            primary: Color::Rgb(130, 170, 255),   // #82AAFF — blue
            secondary: Color::Rgb(199, 146, 234), // #C792EA — purple
            accent: Color::Rgb(255, 203, 107),    // #FFCB6B — yellow
            success: Color::Rgb(195, 232, 141),   // #C3E88D — green
            error: Color::Rgb(255, 83, 112),      // #FF5370 — red
            warning: Color::Rgb(247, 140, 108),   // #F78C6C — orange
            text: Color::Rgb(238, 255, 255),      // #EEFFFF — foreground
            text_muted: Color::Rgb(84, 110, 122), // #546E7A — comment
            text_dimmed: Color::Rgb(60, 80, 90),  // darker comment
            bg: Color::Rgb(38, 50, 56),           // #263238 — background
            bg_elevated: Color::Rgb(48, 62, 70),  // lighter
            bg_surface: Color::Rgb(55, 71, 79),   // #37474F — surface
            bg_deep: Color::Rgb(30, 40, 44),      // darker
            bg_user_message: Color::Rgb(48, 62, 70),
            border: Color::Rgb(55, 71, 79),           // #37474F
            border_active: Color::Rgb(130, 170, 255), // blue
            border_subtle: Color::Rgb(44, 58, 64),
            diff_added: Color::Rgb(195, 232, 141),
            diff_removed: Color::Rgb(255, 83, 112),
            diff_context: Color::Rgb(84, 110, 122),
            diff_hunk_header: Color::Rgb(130, 170, 255),
            diff_added_highlight: Color::Rgb(215, 245, 170),
            diff_removed_highlight: Color::Rgb(255, 120, 140),
            diff_added_bg: Color::Rgb(35, 60, 40),
            diff_removed_bg: Color::Rgb(70, 30, 35),
            risk_safe: Color::Rgb(195, 232, 141),
            risk_low: Color::Rgb(130, 170, 255),
            risk_medium: Color::Rgb(255, 203, 107),
            risk_high: Color::Rgb(255, 83, 112),
            risk_critical: Color::Rgb(255, 50, 70),
        }
    }

    pub fn ayu_dark() -> Self {
        Self {
            name: "ayu_dark".into(),
            // Ayu Dark — warm dark theme by dempfi
            primary: Color::Rgb(57, 186, 230), // #39BAE6 — blue/tag
            secondary: Color::Rgb(210, 166, 255), // #D2A6FF — purple/constant
            accent: Color::Rgb(255, 180, 84),  // #FFB454 — function/orange
            success: Color::Rgb(170, 217, 76), // #AAD94C — string/green
            error: Color::Rgb(240, 113, 120),  // #F07178 — red
            warning: Color::Rgb(232, 182, 74), // #E8B64A — yellow
            text: Color::Rgb(189, 191, 192),   // #BFBFC0 — foreground (editor.fg)
            text_muted: Color::Rgb(107, 114, 121), // #6B7279 — comment
            text_dimmed: Color::Rgb(73, 79, 84), // darker muted
            bg: Color::Rgb(11, 14, 20),        // #0B0E14 — background
            bg_elevated: Color::Rgb(20, 24, 33), // #141821 — panel
            bg_surface: Color::Rgb(30, 35, 46), // surface
            bg_deep: Color::Rgb(6, 8, 13),     // deeper
            bg_user_message: Color::Rgb(20, 24, 33),
            border: Color::Rgb(36, 42, 54), // #242A36 — line/border
            border_active: Color::Rgb(57, 186, 230), // blue
            border_subtle: Color::Rgb(24, 29, 38),
            diff_added: Color::Rgb(170, 217, 76),
            diff_removed: Color::Rgb(240, 113, 120),
            diff_context: Color::Rgb(107, 114, 121),
            diff_hunk_header: Color::Rgb(57, 186, 230),
            diff_added_highlight: Color::Rgb(195, 235, 110),
            diff_removed_highlight: Color::Rgb(255, 145, 150),
            diff_added_bg: Color::Rgb(25, 50, 20),
            diff_removed_bg: Color::Rgb(60, 25, 25),
            risk_safe: Color::Rgb(170, 217, 76),
            risk_low: Color::Rgb(57, 186, 230),
            risk_medium: Color::Rgb(232, 182, 74),
            risk_high: Color::Rgb(240, 113, 120),
            risk_critical: Color::Rgb(255, 80, 85),
        }
    }

    pub fn ayu_mirage() -> Self {
        Self {
            name: "ayu_mirage".into(),
            // Ayu Mirage — cool dark variant by dempfi
            primary: Color::Rgb(95, 180, 229), // #5FB4E5 — tag/blue
            secondary: Color::Rgb(210, 166, 255), // #D2A6FF — constant/purple
            accent: Color::Rgb(255, 211, 130), // #FFD380 — func/orange
            success: Color::Rgb(215, 221, 110), // #D7DD6E — (approx) string/green
            error: Color::Rgb(240, 113, 120),  // #F07178 — red
            warning: Color::Rgb(255, 201, 103), // #FFC967 — yellow
            text: Color::Rgb(204, 204, 204),   // #CCCAC2 — foreground
            text_muted: Color::Rgb(128, 130, 139), // #80828B — comment (B8CFE6 at 40%)
            text_dimmed: Color::Rgb(90, 92, 100), // darker muted
            bg: Color::Rgb(31, 35, 46),        // #1F232E — background
            bg_elevated: Color::Rgb(40, 45, 58), // panel
            bg_surface: Color::Rgb(50, 55, 70), // surface
            bg_deep: Color::Rgb(24, 28, 38),   // deeper
            bg_user_message: Color::Rgb(40, 45, 58),
            border: Color::Rgb(50, 55, 70),          // border
            border_active: Color::Rgb(95, 180, 229), // blue
            border_subtle: Color::Rgb(38, 42, 54),
            diff_added: Color::Rgb(215, 221, 110),
            diff_removed: Color::Rgb(240, 113, 120),
            diff_context: Color::Rgb(128, 130, 139),
            diff_hunk_header: Color::Rgb(95, 180, 229),
            diff_added_highlight: Color::Rgb(230, 240, 140),
            diff_removed_highlight: Color::Rgb(255, 145, 150),
            diff_added_bg: Color::Rgb(35, 50, 30),
            diff_removed_bg: Color::Rgb(60, 30, 30),
            risk_safe: Color::Rgb(215, 221, 110),
            risk_low: Color::Rgb(95, 180, 229),
            risk_medium: Color::Rgb(255, 201, 103),
            risk_high: Color::Rgb(240, 113, 120),
            risk_critical: Color::Rgb(255, 80, 85),
        }
    }

    pub fn everforest() -> Self {
        Self {
            name: "everforest".into(),
            // Everforest Dark — soft green-toned forest theme by sainnhe
            primary: Color::Rgb(127, 187, 179), // #7FBBB3 — blue/aqua
            secondary: Color::Rgb(214, 153, 182), // #D699B6 — purple
            accent: Color::Rgb(219, 188, 127),  // #DBBC7F — yellow
            success: Color::Rgb(167, 192, 128), // #A7C080 — green
            error: Color::Rgb(230, 126, 128),   // #E67E80 — red
            warning: Color::Rgb(230, 152, 117), // #E69875 — orange (or #E69C75)
            text: Color::Rgb(211, 198, 170),    // #D3C6AA — foreground
            text_muted: Color::Rgb(133, 146, 137), // #859289 — grey1
            text_dimmed: Color::Rgb(106, 118, 110), // #6A766E — grey0
            bg: Color::Rgb(47, 53, 47),         // #2F352F — bg0 (medium)
            bg_elevated: Color::Rgb(55, 62, 55), // #374037 — bg1 (approx)
            bg_surface: Color::Rgb(62, 70, 62), // bg2 (approx)
            bg_deep: Color::Rgb(39, 44, 39),    // #272C27 — bg_dim
            bg_user_message: Color::Rgb(55, 62, 55),
            border: Color::Rgb(78, 86, 78),           // visible on bg
            border_active: Color::Rgb(127, 187, 179), // aqua
            border_subtle: Color::Rgb(55, 62, 55),
            diff_added: Color::Rgb(167, 192, 128),
            diff_removed: Color::Rgb(230, 126, 128),
            diff_context: Color::Rgb(133, 146, 137),
            diff_hunk_header: Color::Rgb(127, 187, 179),
            diff_added_highlight: Color::Rgb(190, 215, 155),
            diff_removed_highlight: Color::Rgb(250, 155, 155),
            diff_added_bg: Color::Rgb(45, 60, 40),
            diff_removed_bg: Color::Rgb(65, 35, 35),
            risk_safe: Color::Rgb(167, 192, 128),
            risk_low: Color::Rgb(127, 187, 179),
            risk_medium: Color::Rgb(219, 188, 127),
            risk_high: Color::Rgb(230, 126, 128),
            risk_critical: Color::Rgb(250, 90, 90),
        }
    }

    pub fn nightfox() -> Self {
        Self {
            name: "nightfox".into(),
            // Nightfox — deep blue night theme by EdenEast
            primary: Color::Rgb(113, 156, 219), // #719CDB — blue (or #71839B)
            secondary: Color::Rgb(179, 142, 214), // #B38ED6 — magenta/purple
            accent: Color::Rgb(220, 191, 118),  // #DCBF76 — yellow
            success: Color::Rgb(129, 178, 110), // #81B26E — green
            error: Color::Rgb(196, 99, 113),    // #C46371 — red
            warning: Color::Rgb(218, 165, 100), // #DAA564 — orange
            text: Color::Rgb(205, 207, 216),    // #CDCFD8 — foreground
            text_muted: Color::Rgb(115, 125, 148), // #737D94 — comment
            text_dimmed: Color::Rgb(82, 92, 112), // darker comment
            bg: Color::Rgb(25, 30, 46),         // #192330 — background
            bg_elevated: Color::Rgb(33, 40, 58), // #21283A — lighter
            bg_surface: Color::Rgb(41, 50, 70), // surface
            bg_deep: Color::Rgb(19, 24, 38),    // deeper
            bg_user_message: Color::Rgb(33, 40, 58),
            border: Color::Rgb(44, 55, 75),           // visible
            border_active: Color::Rgb(113, 156, 219), // blue
            border_subtle: Color::Rgb(33, 40, 58),
            diff_added: Color::Rgb(129, 178, 110),
            diff_removed: Color::Rgb(196, 99, 113),
            diff_context: Color::Rgb(115, 125, 148),
            diff_hunk_header: Color::Rgb(113, 156, 219),
            diff_added_highlight: Color::Rgb(155, 205, 140),
            diff_removed_highlight: Color::Rgb(225, 130, 140),
            diff_added_bg: Color::Rgb(25, 50, 30),
            diff_removed_bg: Color::Rgb(60, 25, 30),
            risk_safe: Color::Rgb(129, 178, 110),
            risk_low: Color::Rgb(113, 156, 219),
            risk_medium: Color::Rgb(220, 191, 118),
            risk_high: Color::Rgb(196, 99, 113),
            risk_critical: Color::Rgb(230, 70, 80),
        }
    }

    pub fn github_dark() -> Self {
        Self {
            name: "github_dark".into(),
            // GitHub Dark — github.com dark mode
            primary: Color::Rgb(88, 166, 255),     // #58A6FF — blue
            secondary: Color::Rgb(188, 140, 255),  // #BC8CFF — purple
            accent: Color::Rgb(210, 153, 34),      // #D29922 — yellow
            success: Color::Rgb(63, 185, 80),      // #3FB950 — green
            error: Color::Rgb(248, 81, 73),        // #F85149 — red
            warning: Color::Rgb(210, 153, 34),     // #D29922 — yellow/warning
            text: Color::Rgb(230, 237, 243),       // #E6EDF3 — foreground
            text_muted: Color::Rgb(125, 133, 144), // #7D8590 — muted
            text_dimmed: Color::Rgb(72, 79, 88),   // #484F58
            bg: Color::Rgb(13, 17, 23),            // #0D1117 — background
            bg_elevated: Color::Rgb(22, 27, 34),   // #161B22 — canvas subtle
            bg_surface: Color::Rgb(33, 38, 45),    // #21262D — border default
            bg_deep: Color::Rgb(8, 12, 16),        // deeper
            bg_user_message: Color::Rgb(22, 27, 34),
            border: Color::Rgb(48, 54, 61),          // #30363D
            border_active: Color::Rgb(88, 166, 255), // blue
            border_subtle: Color::Rgb(33, 38, 45),   // #21262D
            diff_added: Color::Rgb(63, 185, 80),
            diff_removed: Color::Rgb(248, 81, 73),
            diff_context: Color::Rgb(125, 133, 144),
            diff_hunk_header: Color::Rgb(88, 166, 255),
            diff_added_highlight: Color::Rgb(70, 220, 100),
            diff_removed_highlight: Color::Rgb(255, 120, 110),
            diff_added_bg: Color::Rgb(26, 71, 33),   // #1A4721
            diff_removed_bg: Color::Rgb(103, 6, 12), // #67060C
            risk_safe: Color::Rgb(63, 185, 80),
            risk_low: Color::Rgb(88, 166, 255),
            risk_medium: Color::Rgb(210, 153, 34),
            risk_high: Color::Rgb(248, 81, 73),
            risk_critical: Color::Rgb(255, 60, 50),
        }
    }

    pub fn moonlight() -> Self {
        Self {
            name: "moonlight".into(),
            // Moonlight II — dreamy cosmic theme by atomiks
            primary: Color::Rgb(130, 170, 255),   // #82AAFF — blue
            secondary: Color::Rgb(195, 142, 255), // #C38EFF — purple (ffc777 alt)
            accent: Color::Rgb(255, 199, 119),    // #FFC777 — yellow/parameter
            success: Color::Rgb(195, 232, 141),   // #C3E88D — green
            error: Color::Rgb(255, 117, 127),     // #FF757F — red
            warning: Color::Rgb(255, 158, 100),   // #FF9E64 — orange
            text: Color::Rgb(200, 211, 245),      // #C8D3F5 — foreground
            text_muted: Color::Rgb(116, 132, 176), // #7485B0 — comment (approx)
            text_dimmed: Color::Rgb(76, 86, 118), // dimmer
            bg: Color::Rgb(34, 36, 54),           // #222436 — background
            bg_elevated: Color::Rgb(44, 47, 68),  // #2C2F44 — lighter
            bg_surface: Color::Rgb(55, 58, 82),   // surface
            bg_deep: Color::Rgb(27, 29, 44),      // #1B1D2C — darker
            bg_user_message: Color::Rgb(44, 47, 68),
            border: Color::Rgb(58, 62, 88),           // #3A3E58
            border_active: Color::Rgb(130, 170, 255), // blue
            border_subtle: Color::Rgb(44, 47, 68),
            diff_added: Color::Rgb(195, 232, 141),
            diff_removed: Color::Rgb(255, 117, 127),
            diff_context: Color::Rgb(116, 132, 176),
            diff_hunk_header: Color::Rgb(130, 170, 255),
            diff_added_highlight: Color::Rgb(215, 245, 170),
            diff_removed_highlight: Color::Rgb(255, 150, 155),
            diff_added_bg: Color::Rgb(35, 55, 35),
            diff_removed_bg: Color::Rgb(65, 30, 35),
            risk_safe: Color::Rgb(195, 232, 141),
            risk_low: Color::Rgb(130, 170, 255),
            risk_medium: Color::Rgb(255, 199, 119),
            risk_high: Color::Rgb(255, 117, 127),
            risk_critical: Color::Rgb(255, 70, 80),
        }
    }

    pub fn synthwave() -> Self {
        Self {
            name: "synthwave".into(),
            // Synthwave '84 — retro 80s neon by Robb Owen
            primary: Color::Rgb(54, 248, 211), // #36F8D3 — cyan/teal
            secondary: Color::Rgb(254, 78, 238), // #FE4EEE — pink/magenta
            accent: Color::Rgb(255, 230, 109), // #FFE66D — yellow
            success: Color::Rgb(114, 247, 164), // #72F7A4 — green
            error: Color::Rgb(254, 78, 111),   // #FE4E6F — red
            warning: Color::Rgb(255, 157, 0),  // #FF9D00 — orange
            text: Color::Rgb(255, 255, 254),   // #FFFFFE — foreground (glow white)
            text_muted: Color::Rgb(132, 138, 184), // #848AB8 — comment
            text_dimmed: Color::Rgb(96, 100, 140), // dimmer
            bg: Color::Rgb(38, 20, 71),        // #261447 — background
            bg_elevated: Color::Rgb(52, 32, 90), // lighter purple
            bg_surface: Color::Rgb(65, 40, 110), // surface
            bg_deep: Color::Rgb(28, 14, 55),   // deeper
            bg_user_message: Color::Rgb(52, 32, 90),
            border: Color::Rgb(75, 48, 120),         // visible
            border_active: Color::Rgb(54, 248, 211), // cyan
            border_subtle: Color::Rgb(52, 32, 90),
            diff_added: Color::Rgb(114, 247, 164),
            diff_removed: Color::Rgb(254, 78, 111),
            diff_context: Color::Rgb(132, 138, 184),
            diff_hunk_header: Color::Rgb(54, 248, 211),
            diff_added_highlight: Color::Rgb(140, 255, 190),
            diff_removed_highlight: Color::Rgb(255, 115, 140),
            diff_added_bg: Color::Rgb(30, 55, 40),
            diff_removed_bg: Color::Rgb(70, 20, 30),
            risk_safe: Color::Rgb(114, 247, 164),
            risk_low: Color::Rgb(54, 248, 211),
            risk_medium: Color::Rgb(255, 230, 109),
            risk_high: Color::Rgb(254, 78, 111),
            risk_critical: Color::Rgb(255, 40, 70),
        }
    }

    pub fn palenight() -> Self {
        Self {
            name: "palenight".into(),
            // Material Palenight — soft purple-blue variant
            primary: Color::Rgb(130, 170, 255),   // #82AAFF — blue
            secondary: Color::Rgb(199, 146, 234), // #C792EA — purple
            accent: Color::Rgb(255, 203, 107),    // #FFCB6B — yellow
            success: Color::Rgb(195, 232, 141),   // #C3E88D — green
            error: Color::Rgb(255, 83, 112),      // #FF5370 — red
            warning: Color::Rgb(247, 140, 108),   // #F78C6C — orange
            text: Color::Rgb(166, 172, 205),      // #A6ACCD — foreground
            text_muted: Color::Rgb(103, 110, 149), // #676E95 — comment
            text_dimmed: Color::Rgb(75, 82, 115), // darker comment
            bg: Color::Rgb(41, 45, 62),           // #292D3E — background
            bg_elevated: Color::Rgb(50, 55, 74),  // #32374A — lighter
            bg_surface: Color::Rgb(60, 65, 85),   // surface
            bg_deep: Color::Rgb(34, 38, 52),      // #222634 — darker
            bg_user_message: Color::Rgb(50, 55, 74),
            border: Color::Rgb(63, 69, 90),           // visible
            border_active: Color::Rgb(130, 170, 255), // blue
            border_subtle: Color::Rgb(48, 53, 70),
            diff_added: Color::Rgb(195, 232, 141),
            diff_removed: Color::Rgb(255, 83, 112),
            diff_context: Color::Rgb(103, 110, 149),
            diff_hunk_header: Color::Rgb(130, 170, 255),
            diff_added_highlight: Color::Rgb(215, 245, 170),
            diff_removed_highlight: Color::Rgb(255, 120, 140),
            diff_added_bg: Color::Rgb(40, 55, 38),
            diff_removed_bg: Color::Rgb(65, 30, 35),
            risk_safe: Color::Rgb(195, 232, 141),
            risk_low: Color::Rgb(130, 170, 255),
            risk_medium: Color::Rgb(255, 203, 107),
            risk_high: Color::Rgb(255, 83, 112),
            risk_critical: Color::Rgb(255, 50, 70),
        }
    }

    pub fn onedark_vivid() -> Self {
        Self {
            name: "onedark_vivid".into(),
            // One Dark Pro Vivid — brighter accent variant
            primary: Color::Rgb(80, 184, 255),    // brighter blue
            secondary: Color::Rgb(210, 120, 240), // brighter purple
            accent: Color::Rgb(240, 200, 100),    // brighter yellow
            success: Color::Rgb(160, 210, 100),   // brighter green
            error: Color::Rgb(240, 90, 100),      // brighter red
            warning: Color::Rgb(225, 160, 85),    // brighter orange
            text: Color::Rgb(171, 178, 191),      // #ABB2BF — foreground
            text_muted: Color::Rgb(92, 99, 112),  // #5C6370 — comment
            text_dimmed: Color::Rgb(63, 68, 80),  // gutter
            bg: Color::Rgb(40, 44, 52),           // #282C34 — background
            bg_elevated: Color::Rgb(50, 56, 66),
            bg_surface: Color::Rgb(62, 68, 81),
            bg_deep: Color::Rgb(33, 37, 43),
            bg_user_message: Color::Rgb(50, 56, 66),
            border: Color::Rgb(62, 68, 81),
            border_active: Color::Rgb(80, 184, 255), // vivid blue
            border_subtle: Color::Rgb(46, 50, 60),
            diff_added: Color::Rgb(160, 210, 100),
            diff_removed: Color::Rgb(240, 90, 100),
            diff_context: Color::Rgb(92, 99, 112),
            diff_hunk_header: Color::Rgb(80, 184, 255),
            diff_added_highlight: Color::Rgb(185, 235, 130),
            diff_removed_highlight: Color::Rgb(255, 125, 130),
            diff_added_bg: Color::Rgb(40, 60, 30),
            diff_removed_bg: Color::Rgb(65, 30, 30),
            risk_safe: Color::Rgb(160, 210, 100),
            risk_low: Color::Rgb(80, 184, 255),
            risk_medium: Color::Rgb(240, 200, 100),
            risk_high: Color::Rgb(240, 90, 100),
            risk_critical: Color::Rgb(255, 60, 70),
        }
    }

    pub fn horizon() -> Self {
        Self {
            name: "horizon".into(),
            // Horizon Dark — warm vibrant dark by jolaleye
            primary: Color::Rgb(38, 187, 217),     // #26BBD9 — cyan
            secondary: Color::Rgb(238, 100, 172),  // #EE64AC — magenta/pink
            accent: Color::Rgb(250, 194, 88),      // #FAC258 — yellow
            success: Color::Rgb(9, 247, 150),      // #09F796 — green
            error: Color::Rgb(232, 109, 120),      // #E86D78 — red (from EC6A88 → toned)
            warning: Color::Rgb(250, 180, 100),    // #FAB464 — orange
            text: Color::Rgb(198, 207, 224), // #C6CFE0 — foreground (approx #D5D8DA → adjusted)
            text_muted: Color::Rgb(107, 114, 128), // #6B7280 — comment (approx)
            text_dimmed: Color::Rgb(78, 84, 96), // darker
            bg: Color::Rgb(28, 30, 38),      // #1C1E26 — background
            bg_elevated: Color::Rgb(37, 39, 50), // #252732 — panel
            bg_surface: Color::Rgb(48, 50, 65), // surface
            bg_deep: Color::Rgb(22, 24, 30), // deeper
            bg_user_message: Color::Rgb(37, 39, 50),
            border: Color::Rgb(55, 58, 74),          // visible
            border_active: Color::Rgb(38, 187, 217), // cyan
            border_subtle: Color::Rgb(37, 39, 50),
            diff_added: Color::Rgb(9, 247, 150),
            diff_removed: Color::Rgb(232, 109, 120),
            diff_context: Color::Rgb(107, 114, 128),
            diff_hunk_header: Color::Rgb(38, 187, 217),
            diff_added_highlight: Color::Rgb(50, 255, 180),
            diff_removed_highlight: Color::Rgb(255, 140, 150),
            diff_added_bg: Color::Rgb(20, 55, 40),
            diff_removed_bg: Color::Rgb(60, 28, 30),
            risk_safe: Color::Rgb(9, 247, 150),
            risk_low: Color::Rgb(38, 187, 217),
            risk_medium: Color::Rgb(250, 194, 88),
            risk_high: Color::Rgb(232, 109, 120),
            risk_critical: Color::Rgb(255, 70, 80),
        }
    }

    pub fn poimandres() -> Self {
        Self {
            name: "poimandres".into(),
            // Poimandres — muted pastels on dark by drcmda
            primary: Color::Rgb(173, 219, 255), // #ADD7FF — blue (property)
            secondary: Color::Rgb(145, 183, 255), // #91B7FF — bright blue
            accent: Color::Rgb(255, 239, 208),  // #FFFFD0 (approx) — yellow string (#FFFAC2)
            success: Color::Rgb(92, 213, 199),  // #5CD5C7 — teal/green
            error: Color::Rgb(208, 99, 142),    // #D0638E — pink/red
            warning: Color::Rgb(255, 198, 146), // #FFC692 — orange (peach)
            text: Color::Rgb(230, 237, 243),    // #E6EDF3 — foreground (approx #E4F0FB)
            text_muted: Color::Rgb(118, 134, 163), // #7686A3 — comment (a6accd dimmer)
            text_dimmed: Color::Rgb(80, 92, 118), // darker
            bg: Color::Rgb(27, 29, 40),         // #1B1D28 — background
            bg_elevated: Color::Rgb(36, 38, 52), // panel
            bg_surface: Color::Rgb(48, 50, 66), // surface
            bg_deep: Color::Rgb(21, 22, 32),    // deeper
            bg_user_message: Color::Rgb(36, 38, 52),
            border: Color::Rgb(52, 55, 72),           // visible
            border_active: Color::Rgb(173, 219, 255), // blue
            border_subtle: Color::Rgb(38, 40, 54),
            diff_added: Color::Rgb(92, 213, 199),
            diff_removed: Color::Rgb(208, 99, 142),
            diff_context: Color::Rgb(118, 134, 163),
            diff_hunk_header: Color::Rgb(173, 219, 255),
            diff_added_highlight: Color::Rgb(120, 235, 220),
            diff_removed_highlight: Color::Rgb(235, 130, 165),
            diff_added_bg: Color::Rgb(25, 50, 48),
            diff_removed_bg: Color::Rgb(60, 28, 40),
            risk_safe: Color::Rgb(92, 213, 199),
            risk_low: Color::Rgb(173, 219, 255),
            risk_medium: Color::Rgb(255, 198, 146),
            risk_high: Color::Rgb(208, 99, 142),
            risk_critical: Color::Rgb(240, 60, 100),
        }
    }

    pub fn vesper() -> Self {
        Self {
            name: "vesper".into(),
            // Vesper — warm amber/orange on deep black by raunofreiberg
            primary: Color::Rgb(255, 199, 119), // #FFC777 — warm amber
            secondary: Color::Rgb(218, 166, 117), // #DAA675 — tan/secondary
            accent: Color::Rgb(255, 139, 56),   // #FF8B38 — orange accent
            success: Color::Rgb(108, 185, 105), // #6CB969 — green (muted)
            error: Color::Rgb(238, 93, 93),     // #EE5D5D — red
            warning: Color::Rgb(255, 180, 84),  // #FFB454 — warm orange
            text: Color::Rgb(190, 186, 174),    // #BEBAAE — foreground (warm gray)
            text_muted: Color::Rgb(128, 124, 112), // #807C70 — comment
            text_dimmed: Color::Rgb(90, 87, 78), // darker
            bg: Color::Rgb(16, 16, 16),         // #101010 — background (deep black)
            bg_elevated: Color::Rgb(28, 28, 26), // slightly lighter
            bg_surface: Color::Rgb(40, 40, 36), // surface
            bg_deep: Color::Rgb(10, 10, 10),    // deeper
            bg_user_message: Color::Rgb(28, 28, 26),
            border: Color::Rgb(50, 50, 45),           // visible
            border_active: Color::Rgb(255, 199, 119), // amber
            border_subtle: Color::Rgb(34, 34, 31),
            diff_added: Color::Rgb(108, 185, 105),
            diff_removed: Color::Rgb(238, 93, 93),
            diff_context: Color::Rgb(128, 124, 112),
            diff_hunk_header: Color::Rgb(255, 199, 119),
            diff_added_highlight: Color::Rgb(135, 210, 130),
            diff_removed_highlight: Color::Rgb(255, 130, 130),
            diff_added_bg: Color::Rgb(30, 45, 28),
            diff_removed_bg: Color::Rgb(55, 22, 22),
            risk_safe: Color::Rgb(108, 185, 105),
            risk_low: Color::Rgb(255, 199, 119),
            risk_medium: Color::Rgb(255, 180, 84),
            risk_high: Color::Rgb(238, 93, 93),
            risk_critical: Color::Rgb(255, 55, 55),
        }
    }

    // -----------------------------------------------------------------------
    // Light themes
    // -----------------------------------------------------------------------

    pub fn github_light() -> Self {
        Self {
            name: "github_light".into(),
            // GitHub Light — github.com light mode
            primary: Color::Rgb(9, 105, 218),       // #0969DA — blue
            secondary: Color::Rgb(130, 80, 223),    // #8250DF — purple
            accent: Color::Rgb(191, 135, 0),        // #BF8700 — yellow
            success: Color::Rgb(26, 127, 55),       // #1A7F37 — green
            error: Color::Rgb(207, 34, 46),         // #CF222E — red
            warning: Color::Rgb(191, 135, 0),       // #BF8700 — yellow
            text: Color::Rgb(31, 35, 40),           // #1F2328 — foreground (fg.default)
            text_muted: Color::Rgb(101, 109, 118),  // #656D76 — muted
            text_dimmed: Color::Rgb(140, 149, 159), // lighter for less emphasis
            bg: Color::Rgb(255, 255, 255),          // #FFFFFF — background
            bg_elevated: Color::Rgb(246, 248, 250), // #F6F8FA — canvas subtle
            bg_surface: Color::Rgb(234, 238, 242),  // #EAEEF2 — surface
            bg_deep: Color::Rgb(255, 255, 255),     // white
            bg_user_message: Color::Rgb(246, 248, 250),
            border: Color::Rgb(208, 215, 222),        // #D0D7DE
            border_active: Color::Rgb(9, 105, 218),   // blue
            border_subtle: Color::Rgb(234, 238, 242), // subtle
            diff_added: Color::Rgb(26, 127, 55),
            diff_removed: Color::Rgb(207, 34, 46),
            diff_context: Color::Rgb(101, 109, 118),
            diff_hunk_header: Color::Rgb(9, 105, 218),
            diff_added_highlight: Color::Rgb(40, 167, 69),
            diff_removed_highlight: Color::Rgb(230, 55, 65),
            diff_added_bg: Color::Rgb(218, 251, 225), // #DAFBE1
            diff_removed_bg: Color::Rgb(255, 235, 233), // #FFEBE9
            risk_safe: Color::Rgb(26, 127, 55),
            risk_low: Color::Rgb(9, 105, 218),
            risk_medium: Color::Rgb(191, 135, 0),
            risk_high: Color::Rgb(207, 34, 46),
            risk_critical: Color::Rgb(180, 20, 30),
        }
    }

    pub fn solarized_light() -> Self {
        Self {
            name: "solarized_light".into(),
            // Solarized Light — Ethan Schoonover's light variant
            primary: Color::Rgb(38, 139, 210),    // #268BD2 — blue
            secondary: Color::Rgb(108, 113, 196), // #6C71C4 — violet
            accent: Color::Rgb(181, 137, 0),      // #B58900 — yellow
            success: Color::Rgb(133, 153, 0),     // #859900 — green
            error: Color::Rgb(220, 50, 47),       // #DC322F — red
            warning: Color::Rgb(203, 75, 22),     // #CB4B16 — orange
            text: Color::Rgb(101, 123, 131),      // #657B83 — base00 (body text)
            text_muted: Color::Rgb(88, 110, 117), // #586E75 — base01
            text_dimmed: Color::Rgb(147, 161, 161), // #93A1A1 — base1
            bg: Color::Rgb(253, 246, 227),        // #FDF6E3 — base3 (background)
            bg_elevated: Color::Rgb(238, 232, 213), // #EEE8D5 — base2
            bg_surface: Color::Rgb(225, 219, 200), // lighter surface
            bg_deep: Color::Rgb(253, 246, 227),   // base3
            bg_user_message: Color::Rgb(238, 232, 213), // base2
            border: Color::Rgb(210, 205, 188),    // visible on base3
            border_active: Color::Rgb(38, 139, 210), // blue
            border_subtle: Color::Rgb(230, 224, 206), // subtle
            diff_added: Color::Rgb(133, 153, 0),
            diff_removed: Color::Rgb(220, 50, 47),
            diff_context: Color::Rgb(88, 110, 117),
            diff_hunk_header: Color::Rgb(38, 139, 210),
            diff_added_highlight: Color::Rgb(160, 180, 40),
            diff_removed_highlight: Color::Rgb(245, 80, 75),
            diff_added_bg: Color::Rgb(225, 242, 210),
            diff_removed_bg: Color::Rgb(252, 222, 218),
            risk_safe: Color::Rgb(133, 153, 0),
            risk_low: Color::Rgb(38, 139, 210),
            risk_medium: Color::Rgb(181, 137, 0),
            risk_high: Color::Rgb(220, 50, 47),
            risk_critical: Color::Rgb(190, 30, 30),
        }
    }

    pub fn catppuccin_latte() -> Self {
        Self {
            name: "catppuccin_latte".into(),
            // Catppuccin Latte — light pastel variant
            primary: Color::Rgb(30, 102, 245),     // #1E66F5 — blue
            secondary: Color::Rgb(136, 57, 239),   // #8839EF — mauve
            accent: Color::Rgb(223, 142, 29),      // #DF8E1D — yellow
            success: Color::Rgb(64, 160, 43),      // #40A02B — green
            error: Color::Rgb(210, 15, 57),        // #D20F39 — red
            warning: Color::Rgb(254, 100, 11),     // #FE640B — peach
            text: Color::Rgb(76, 79, 105),         // #4C4F69 — text
            text_muted: Color::Rgb(108, 111, 133), // #6C6F85 — subtext0
            text_dimmed: Color::Rgb(156, 160, 176), // #9CA0B0 — overlay0
            bg: Color::Rgb(239, 241, 245),         // #EFF1F5 — base
            bg_elevated: Color::Rgb(230, 233, 239), // #E6E9EF — mantle
            bg_surface: Color::Rgb(220, 224, 232), // #DCE0E8 — crust
            bg_deep: Color::Rgb(239, 241, 245),    // base
            bg_user_message: Color::Rgb(230, 233, 239), // mantle
            border: Color::Rgb(204, 208, 218),     // #CCD0DA — surface0
            border_active: Color::Rgb(30, 102, 245), // blue
            border_subtle: Color::Rgb(220, 224, 232), // crust
            diff_added: Color::Rgb(64, 160, 43),
            diff_removed: Color::Rgb(210, 15, 57),
            diff_context: Color::Rgb(108, 111, 133),
            diff_hunk_header: Color::Rgb(30, 102, 245),
            diff_added_highlight: Color::Rgb(80, 190, 60),
            diff_removed_highlight: Color::Rgb(235, 40, 80),
            diff_added_bg: Color::Rgb(215, 240, 210),
            diff_removed_bg: Color::Rgb(250, 215, 220),
            risk_safe: Color::Rgb(64, 160, 43),
            risk_low: Color::Rgb(30, 102, 245),
            risk_medium: Color::Rgb(223, 142, 29),
            risk_high: Color::Rgb(210, 15, 57),
            risk_critical: Color::Rgb(180, 10, 40),
        }
    }

    pub fn one_light() -> Self {
        Self {
            name: "one_light".into(),
            // One Light — Atom's light theme
            primary: Color::Rgb(64, 120, 242),     // #4078F2 — blue
            secondary: Color::Rgb(166, 38, 164),   // #A626A4 — purple
            accent: Color::Rgb(193, 132, 1),       // #C18401 — yellow
            success: Color::Rgb(80, 161, 79),      // #50A14F — green
            error: Color::Rgb(228, 86, 73),        // #E45649 — red
            warning: Color::Rgb(152, 104, 1),      // #986801 — dark yellow/orange
            text: Color::Rgb(56, 58, 66),          // #383A42 — foreground
            text_muted: Color::Rgb(106, 115, 125), // #6A737D — comment (approx #A0A1A7)
            text_dimmed: Color::Rgb(160, 161, 167), // #A0A1A7 — lighter
            bg: Color::Rgb(250, 250, 250),         // #FAFAFA — background
            bg_elevated: Color::Rgb(240, 240, 240), // lighter panel
            bg_surface: Color::Rgb(226, 229, 233), // #E2E5E9 — surface (approx)
            bg_deep: Color::Rgb(250, 250, 250),    // same as bg
            bg_user_message: Color::Rgb(240, 240, 240),
            border: Color::Rgb(219, 219, 219), // #DBDBDB — border
            border_active: Color::Rgb(64, 120, 242), // blue
            border_subtle: Color::Rgb(234, 234, 234),
            diff_added: Color::Rgb(80, 161, 79),
            diff_removed: Color::Rgb(228, 86, 73),
            diff_context: Color::Rgb(106, 115, 125),
            diff_hunk_header: Color::Rgb(64, 120, 242),
            diff_added_highlight: Color::Rgb(100, 190, 100),
            diff_removed_highlight: Color::Rgb(250, 110, 100),
            diff_added_bg: Color::Rgb(215, 240, 210),
            diff_removed_bg: Color::Rgb(252, 225, 222),
            risk_safe: Color::Rgb(80, 161, 79),
            risk_low: Color::Rgb(64, 120, 242),
            risk_medium: Color::Rgb(193, 132, 1),
            risk_high: Color::Rgb(228, 86, 73),
            risk_critical: Color::Rgb(200, 50, 40),
        }
    }

    pub fn rose_pine_dawn() -> Self {
        Self {
            name: "rose_pine_dawn".into(),
            // Rose Pine Dawn — light variant with rose accents
            primary: Color::Rgb(40, 105, 131),     // #286983 — pine
            secondary: Color::Rgb(144, 122, 169),  // #907AA9 — iris
            accent: Color::Rgb(215, 130, 126),     // #D7827E — rose
            success: Color::Rgb(40, 105, 131),     // #286983 — pine (used as success)
            error: Color::Rgb(180, 99, 122),       // #B4637A — love
            warning: Color::Rgb(234, 157, 52),     // #EA9D34 — gold
            text: Color::Rgb(87, 82, 121),         // #575279 — text
            text_muted: Color::Rgb(121, 117, 147), // #797593 — subtle
            text_dimmed: Color::Rgb(152, 147, 165), // #9893A5 — muted
            bg: Color::Rgb(250, 244, 237),         // #FAF4ED — base
            bg_elevated: Color::Rgb(242, 233, 222), // #F2E9DE — overlay
            bg_surface: Color::Rgb(255, 250, 243), // #FFFAF3 — surface
            bg_deep: Color::Rgb(250, 244, 237),    // base
            bg_user_message: Color::Rgb(242, 233, 222), // overlay
            border: Color::Rgb(223, 218, 210),     // #DFDAD2 — highlight med
            border_active: Color::Rgb(40, 105, 131), // pine
            border_subtle: Color::Rgb(242, 233, 222), // overlay
            diff_added: Color::Rgb(40, 105, 131),
            diff_removed: Color::Rgb(180, 99, 122),
            diff_context: Color::Rgb(121, 117, 147),
            diff_hunk_header: Color::Rgb(40, 105, 131),
            diff_added_highlight: Color::Rgb(55, 135, 160),
            diff_removed_highlight: Color::Rgb(210, 120, 140),
            diff_added_bg: Color::Rgb(218, 240, 235),
            diff_removed_bg: Color::Rgb(250, 220, 225),
            risk_safe: Color::Rgb(40, 105, 131),
            risk_low: Color::Rgb(40, 105, 131),
            risk_medium: Color::Rgb(234, 157, 52),
            risk_high: Color::Rgb(180, 99, 122),
            risk_critical: Color::Rgb(160, 60, 80),
        }
    }

    /// Look up a theme by name. Built-in themes take priority over custom themes.
    pub fn from_name(name: &str) -> Self {
        match name {
            "default" => Self::default_theme(),
            "dracula" => Self::dracula(),
            "nord" => Self::nord(),
            "gruvbox" => Self::gruvbox(),
            "catppuccin" => Self::catppuccin(),
            "solarized_dark" => Self::solarized_dark(),
            "tokyo_night" => Self::tokyo_night(),
            "one_dark" => Self::one_dark(),
            "rose_pine" => Self::rose_pine(),
            "kanagawa" => Self::kanagawa(),
            "monokai" => Self::monokai(),
            "material" => Self::material(),
            "ayu_dark" => Self::ayu_dark(),
            "ayu_mirage" => Self::ayu_mirage(),
            "everforest" => Self::everforest(),
            "nightfox" => Self::nightfox(),
            "github_dark" => Self::github_dark(),
            "moonlight" => Self::moonlight(),
            "synthwave" => Self::synthwave(),
            "palenight" => Self::palenight(),
            "onedark_vivid" => Self::onedark_vivid(),
            "horizon" => Self::horizon(),
            "poimandres" => Self::poimandres(),
            "vesper" => Self::vesper(),
            "github_light" => Self::github_light(),
            "solarized_light" => Self::solarized_light(),
            "catppuccin_latte" => Self::catppuccin_latte(),
            "one_light" => Self::one_light(),
            "rose_pine_dawn" => Self::rose_pine_dawn(),
            _ => {
                // Check custom themes
                if let Some(custom) = custom_themes().iter().find(|t| t.name == name) {
                    return custom.clone();
                }
                Self::default_theme()
            }
        }
    }

    /// All available theme names (built-in + custom) in cycle order.
    pub fn all_names() -> Vec<String> {
        let mut names: Vec<String> = BUILTIN_NAMES.iter().map(|&s| s.to_string()).collect();
        for custom in custom_themes() {
            // Skip custom themes that shadow built-in names
            if !BUILTIN_NAMES.contains(&custom.name.as_str()) {
                names.push(custom.name.clone());
            }
        }
        names
    }

    /// Return the next theme in cycle order after the current one.
    pub fn next(&self) -> Self {
        let names = Self::all_names();
        let current_idx = names.iter().position(|n| n == &self.name).unwrap_or(0);
        let next_idx = (current_idx + 1) % names.len();
        Self::from_name(&names[next_idx])
    }

    // -----------------------------------------------------------------------
    // Custom theme loading
    // -----------------------------------------------------------------------

    /// Load a custom theme from a TOML file. Missing color fields fall back
    /// to the default theme's values.
    pub fn load_custom(path: &Path) -> Result<Self, String> {
        let content = std::fs::read_to_string(path)
            .map_err(|e| format!("Failed to read {}: {e}", path.display()))?;
        Self::from_toml(&content)
    }

    /// Parse a TOML string into a Theme. Useful for testing without files.
    pub fn from_toml(toml_str: &str) -> Result<Self, String> {
        let file: ThemeFile =
            toml::from_str(toml_str).map_err(|e| format!("Failed to parse theme TOML: {e}"))?;

        let defaults = Self::default_theme();
        let c = &file.colors;

        Ok(Self {
            name: file.name,
            primary: resolve_color(&c.primary, defaults.primary),
            secondary: resolve_color(&c.secondary, defaults.secondary),
            accent: resolve_color(&c.accent, defaults.accent),
            success: resolve_color(&c.success, defaults.success),
            error: resolve_color(&c.error, defaults.error),
            warning: resolve_color(&c.warning, defaults.warning),
            text: resolve_color(&c.text, defaults.text),
            text_muted: resolve_color(&c.text_muted, defaults.text_muted),
            text_dimmed: resolve_color(&c.text_dimmed, defaults.text_dimmed),
            bg: resolve_color(&c.bg, defaults.bg),
            bg_elevated: resolve_color(&c.bg_elevated, defaults.bg_elevated),
            bg_surface: resolve_color(&c.bg_surface, defaults.bg_surface),
            bg_deep: resolve_color(&c.bg_deep, defaults.bg_deep),
            bg_user_message: resolve_color(&c.bg_user_message, defaults.bg_user_message),
            border: resolve_color(&c.border, defaults.border),
            border_active: resolve_color(&c.border_active, defaults.border_active),
            border_subtle: resolve_color(&c.border_subtle, defaults.border_subtle),
            diff_added: resolve_color(&c.diff_added, defaults.diff_added),
            diff_removed: resolve_color(&c.diff_removed, defaults.diff_removed),
            diff_context: resolve_color(&c.diff_context, defaults.diff_context),
            diff_hunk_header: resolve_color(&c.diff_hunk_header, defaults.diff_hunk_header),
            diff_added_highlight: resolve_color(
                &c.diff_added_highlight,
                defaults.diff_added_highlight,
            ),
            diff_removed_highlight: resolve_color(
                &c.diff_removed_highlight,
                defaults.diff_removed_highlight,
            ),
            diff_added_bg: resolve_color(&c.diff_added_bg, defaults.diff_added_bg),
            diff_removed_bg: resolve_color(&c.diff_removed_bg, defaults.diff_removed_bg),
            risk_safe: resolve_color(&c.risk_safe, defaults.risk_safe),
            risk_low: resolve_color(&c.risk_low, defaults.risk_low),
            risk_medium: resolve_color(&c.risk_medium, defaults.risk_medium),
            risk_high: resolve_color(&c.risk_high, defaults.risk_high),
            risk_critical: resolve_color(&c.risk_critical, defaults.risk_critical),
        })
    }

    /// Scan `~/.ava/themes/` for `.toml` files and load them all.
    /// Errors in individual files are logged and skipped.
    pub fn load_all_custom() -> Vec<Self> {
        let Some(themes_dir) = themes_dir() else {
            return Vec::new();
        };

        if !themes_dir.is_dir() {
            return Vec::new();
        }

        let mut themes = Vec::new();
        let entries = match std::fs::read_dir(&themes_dir) {
            Ok(entries) => entries,
            Err(_) => return Vec::new(),
        };

        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().is_some_and(|ext| ext == "toml") {
                match Self::load_custom(&path) {
                    Ok(theme) => themes.push(theme),
                    Err(e) => {
                        tracing::warn!("Skipping theme {}: {e}", path.display());
                    }
                }
            }
        }

        // Sort by name for deterministic ordering
        themes.sort_by(|a, b| a.name.cmp(&b.name));
        themes
    }
}

/// Return the path to `~/.ava/themes/`.
fn themes_dir() -> Option<PathBuf> {
    dirs::home_dir().map(|h| h.join(".ava").join("themes"))
}

/// Get the cached custom themes, loading them on first access.
fn custom_themes() -> &'static Vec<Theme> {
    CUSTOM_THEMES.get_or_init(Theme::load_all_custom)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_hex_color_with_hash() {
        assert_eq!(parse_hex_color("#FF0000"), Some(Color::Rgb(255, 0, 0)));
    }

    #[test]
    fn parse_hex_color_without_hash() {
        assert_eq!(parse_hex_color("00FF00"), Some(Color::Rgb(0, 255, 0)));
    }

    #[test]
    fn parse_hex_color_invalid() {
        assert_eq!(parse_hex_color("nope"), None);
        assert_eq!(parse_hex_color("#GG0000"), None);
        assert_eq!(parse_hex_color("#FFF"), None);
    }

    #[test]
    fn from_toml_full() {
        let toml = r##"
name = "my-theme"

[colors]
primary = "#FF0000"
secondary = "00FF00"
bg = "#000000"
text = "#FFFFFF"
"##;
        let theme = Theme::from_toml(toml).unwrap();
        assert_eq!(theme.name, "my-theme");
        assert_eq!(theme.primary, Color::Rgb(255, 0, 0));
        assert_eq!(theme.secondary, Color::Rgb(0, 255, 0));
        assert_eq!(theme.bg, Color::Rgb(0, 0, 0));
        assert_eq!(theme.text, Color::Rgb(255, 255, 255));
        // Unspecified fields should fall back to default
        assert_eq!(theme.accent, Theme::default_theme().accent);
    }

    #[test]
    fn from_toml_minimal() {
        let toml = r##"
name = "minimal"
"##;
        let theme = Theme::from_toml(toml).unwrap();
        assert_eq!(theme.name, "minimal");
        // All colors should be defaults
        assert_eq!(theme.primary, Theme::default_theme().primary);
    }

    #[test]
    fn from_toml_invalid_color_falls_back() {
        let toml = r##"
name = "bad-colors"

[colors]
primary = "not-a-color"
secondary = "#FF0000"
"##;
        let theme = Theme::from_toml(toml).unwrap();
        // Invalid color falls back to default
        assert_eq!(theme.primary, Theme::default_theme().primary);
        // Valid color is parsed
        assert_eq!(theme.secondary, Color::Rgb(255, 0, 0));
    }

    #[test]
    fn from_toml_missing_name_fails() {
        let toml = r##"
[colors]
primary = "#FF0000"
"##;
        assert!(Theme::from_toml(toml).is_err());
    }

    #[test]
    fn builtin_themes_load() {
        assert_eq!(Theme::from_name("default").name, "default");
        assert_eq!(Theme::from_name("dracula").name, "dracula");
        assert_eq!(Theme::from_name("nord").name, "nord");
        assert_eq!(Theme::from_name("nonexistent").name, "default");
    }

    #[test]
    fn all_names_includes_builtins() {
        let names = Theme::all_names();
        assert!(names.contains(&"default".to_string()));
        assert!(names.contains(&"dracula".to_string()));
        assert!(names.contains(&"nord".to_string()));
    }

    #[test]
    fn theme_next_cycles() {
        let theme = Theme::default_theme();
        let next = theme.next();
        assert_eq!(next.name, "dracula");
        let next2 = next.next();
        assert_eq!(next2.name, "nord");
    }
}
