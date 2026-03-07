use ratatui::style::Color;

#[derive(Debug, Clone)]
pub struct Theme {
    pub name: &'static str,
    pub primary: Color,
    pub secondary: Color,
    pub accent: Color,
    pub error: Color,
    pub warning: Color,
    pub text: Color,
    pub text_muted: Color,
    pub border: Color,
    pub bg: Color,
    pub diff_added: Color,
    pub diff_removed: Color,
    pub diff_context: Color,
    pub diff_hunk_header: Color,
}

impl Theme {
    pub fn default_theme() -> Self {
        Self {
            name: "default",
            primary: Color::Cyan,
            secondary: Color::Blue,
            accent: Color::Yellow,
            error: Color::Red,
            warning: Color::LightYellow,
            text: Color::White,
            text_muted: Color::Gray,
            border: Color::DarkGray,
            bg: Color::Black,
            diff_added: Color::Green,
            diff_removed: Color::Red,
            diff_context: Color::Gray,
            diff_hunk_header: Color::Cyan,
        }
    }

    pub fn dracula() -> Self {
        Self {
            name: "dracula",
            primary: Color::Rgb(139, 233, 253),
            secondary: Color::Rgb(80, 250, 123),
            accent: Color::Rgb(241, 250, 140),
            error: Color::Rgb(255, 85, 85),
            warning: Color::Rgb(255, 184, 108),
            text: Color::Rgb(248, 248, 242),
            text_muted: Color::Rgb(98, 114, 164),
            border: Color::Rgb(68, 71, 90),
            bg: Color::Rgb(40, 42, 54),
            diff_added: Color::Rgb(80, 250, 123),
            diff_removed: Color::Rgb(255, 85, 85),
            diff_context: Color::Rgb(98, 114, 164),
            diff_hunk_header: Color::Rgb(139, 233, 253),
        }
    }

    pub fn nord() -> Self {
        Self {
            name: "nord",
            primary: Color::Rgb(136, 192, 208),
            secondary: Color::Rgb(129, 161, 193),
            accent: Color::Rgb(235, 203, 139),
            error: Color::Rgb(191, 97, 106),
            warning: Color::Rgb(208, 135, 112),
            text: Color::Rgb(236, 239, 244),
            text_muted: Color::Rgb(76, 86, 106),
            border: Color::Rgb(67, 76, 94),
            bg: Color::Rgb(46, 52, 64),
            diff_added: Color::Rgb(163, 190, 140),
            diff_removed: Color::Rgb(191, 97, 106),
            diff_context: Color::Rgb(129, 161, 193),
            diff_hunk_header: Color::Rgb(136, 192, 208),
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
