use ratatui::style::Color;

#[derive(Debug, Clone)]
pub struct Theme {
    pub name: &'static str,
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
    pub bg_user_message: Color,
    // Borders
    pub border: Color,
    pub border_active: Color,
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

impl Theme {
    pub fn default_theme() -> Self {
        Self {
            name: "default",
            // GitHub dark-inspired palette
            primary: Color::Rgb(88, 166, 255),    // #58a6ff — blue
            secondary: Color::Rgb(121, 192, 255),  // #79c0ff — light blue
            accent: Color::Rgb(240, 136, 62),      // #f0883e — amber
            success: Color::Rgb(63, 185, 80),      // #3fb950 — green
            error: Color::Rgb(248, 81, 73),        // #f85149 — red
            warning: Color::Rgb(210, 153, 34),     // #d29922 — gold
            // Text
            text: Color::Rgb(230, 237, 243),       // #e6edf3 — soft white
            text_muted: Color::Rgb(125, 133, 144), // #7d8590 — gray
            text_dimmed: Color::Rgb(72, 79, 88),   // #484f58 — dark gray
            // Surfaces
            bg: Color::Rgb(13, 17, 23),            // #0d1117
            bg_elevated: Color::Rgb(22, 27, 34),   // #161b22
            bg_user_message: Color::Rgb(22, 27, 34), // #161b22 — subtle tint
            // Borders
            border: Color::Rgb(48, 54, 61),        // #30363d
            border_active: Color::Rgb(88, 166, 255), // #58a6ff
            // Diff
            diff_added: Color::Rgb(63, 185, 80),   // #3fb950
            diff_removed: Color::Rgb(248, 81, 73), // #f85149
            diff_context: Color::Rgb(125, 133, 144), // #7d8590
            diff_hunk_header: Color::Rgb(88, 166, 255), // #58a6ff
            diff_added_highlight: Color::Rgb(70, 220, 100), // brighter green
            diff_removed_highlight: Color::Rgb(255, 120, 110), // brighter red
            diff_added_bg: Color::Rgb(26, 71, 33), // #1a4721
            diff_removed_bg: Color::Rgb(103, 6, 12), // #67060c
            // Risk
            risk_safe: Color::Rgb(63, 185, 80),    // success
            risk_low: Color::Rgb(88, 166, 255),    // primary
            risk_medium: Color::Rgb(210, 153, 34), // warning
            risk_high: Color::Rgb(248, 81, 73),    // error
            risk_critical: Color::Rgb(255, 60, 50), // bright red
        }
    }

    pub fn dracula() -> Self {
        Self {
            name: "dracula",
            primary: Color::Rgb(139, 233, 253),   // cyan
            secondary: Color::Rgb(80, 250, 123),   // green
            accent: Color::Rgb(255, 184, 108),     // orange
            success: Color::Rgb(80, 250, 123),     // green
            error: Color::Rgb(255, 85, 85),        // red
            warning: Color::Rgb(241, 250, 140),    // yellow
            text: Color::Rgb(248, 248, 242),       // foreground
            text_muted: Color::Rgb(98, 114, 164),  // comment
            text_dimmed: Color::Rgb(68, 71, 90),   // current line
            bg: Color::Rgb(40, 42, 54),            // background
            bg_elevated: Color::Rgb(50, 52, 66),   // slightly lighter
            bg_user_message: Color::Rgb(50, 52, 66),
            border: Color::Rgb(68, 71, 90),        // current line
            border_active: Color::Rgb(139, 233, 253), // cyan
            diff_added: Color::Rgb(80, 250, 123),
            diff_removed: Color::Rgb(255, 85, 85),
            diff_context: Color::Rgb(98, 114, 164),
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
            name: "nord",
            primary: Color::Rgb(136, 192, 208),    // nord8 frost
            secondary: Color::Rgb(129, 161, 193),  // nord9
            accent: Color::Rgb(235, 203, 139),     // nord13 yellow
            success: Color::Rgb(163, 190, 140),    // nord14 green
            error: Color::Rgb(191, 97, 106),       // nord11 red
            warning: Color::Rgb(208, 135, 112),    // nord12 orange
            text: Color::Rgb(236, 239, 244),       // nord6 snow
            text_muted: Color::Rgb(76, 86, 106),   // nord3
            text_dimmed: Color::Rgb(59, 66, 82),   // nord2
            bg: Color::Rgb(46, 52, 64),            // nord0 polar night
            bg_elevated: Color::Rgb(59, 66, 82),   // nord2
            bg_user_message: Color::Rgb(59, 66, 82),
            border: Color::Rgb(67, 76, 94),        // nord3
            border_active: Color::Rgb(136, 192, 208), // nord8
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

    pub fn from_name(name: &str) -> Self {
        match name {
            "dracula" => Self::dracula(),
            "nord" => Self::nord(),
            _ => Self::default_theme(),
        }
    }
}
