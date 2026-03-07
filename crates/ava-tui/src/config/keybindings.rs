use crate::state::keybinds::{Action, KeyBinding};
use color_eyre::Result;
use crossterm::event::{KeyCode, KeyModifiers};
use serde::Deserialize;
use std::collections::HashMap;

#[derive(Debug, Deserialize)]
struct RawBindings(HashMap<String, Vec<String>>);

pub fn load_keybind_overrides() -> Result<HashMap<Action, Vec<KeyBinding>>> {
    let Some(home) = dirs::home_dir() else {
        return Ok(HashMap::new());
    };
    let path = home.join(".ava/keybindings.json");
    if !path.exists() {
        return Ok(HashMap::new());
    }

    let content = std::fs::read_to_string(path)?;
    let raw: RawBindings = serde_json::from_str(&content)?;
    let mut map = HashMap::new();

    for (action_name, keys) in raw.0 {
        let Some(action) = parse_action(&action_name) else {
            continue;
        };
        let parsed = keys
            .into_iter()
            .filter_map(|k| parse_binding(&k))
            .collect::<Vec<_>>();
        if !parsed.is_empty() {
            map.insert(action, parsed);
        }
    }

    Ok(map)
}

fn parse_action(name: &str) -> Option<Action> {
    match name {
        "command_palette" => Some(Action::CommandPalette),
        "new_session" => Some(Action::NewSession),
        "session_list" => Some(Action::SessionList),
        "model_switch" => Some(Action::ModelSwitch),
        "scroll_up" => Some(Action::ScrollUp),
        "scroll_down" => Some(Action::ScrollDown),
        "scroll_top" => Some(Action::ScrollTop),
        "scroll_bottom" => Some(Action::ScrollBottom),
        "toggle_sidebar" => Some(Action::ToggleSidebar),
        "toggle_thinking" => Some(Action::ToggleThinking),
        "cancel" => Some(Action::Cancel),
        "quit" => Some(Action::Quit),
        "yolo_toggle" => Some(Action::YoloToggle),
        _ => None,
    }
}

fn parse_binding(binding: &str) -> Option<KeyBinding> {
    let lower = binding.to_lowercase();
    let mut parts = lower.split('+').collect::<Vec<_>>();
    let key = parts.pop()?;
    let mut modifiers = KeyModifiers::empty();
    for part in parts {
        if part == "ctrl" {
            modifiers |= KeyModifiers::CONTROL;
        }
    }

    let code = match key {
        "pageup" => KeyCode::PageUp,
        "pagedown" => KeyCode::PageDown,
        "home" => KeyCode::Home,
        "end" => KeyCode::End,
        _ if key.len() == 1 => KeyCode::Char(key.chars().next()?),
        _ => return None,
    };

    Some(KeyBinding { code, modifiers })
}
