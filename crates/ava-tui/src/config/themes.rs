use crate::state::theme::Theme;

pub fn load_theme(name: &str) -> Theme {
    Theme::from_name(name)
}
