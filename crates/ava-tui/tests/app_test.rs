use ava_tui::event::tick_interval;
use ava_tui::state::theme::Theme;
use std::time::Duration;

#[test]
fn tick_interval_switches_for_streaming() {
    assert_eq!(tick_interval(true), Duration::from_millis(16));
    assert_eq!(tick_interval(false), Duration::from_millis(250));
}

#[test]
fn theme_loading_works() {
    assert_eq!(Theme::from_name("dracula").name, "dracula");
    assert_eq!(Theme::from_name("nord").name, "nord");
    assert_eq!(Theme::from_name("unknown").name, "default");
}
