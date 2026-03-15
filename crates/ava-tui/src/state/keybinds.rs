use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum Action {
    CommandPalette,
    NewSession,
    SessionList,
    ModelSwitch,
    ScrollUp,
    ScrollDown,
    ScrollTop,
    ScrollBottom,
    ToggleSidebar,
    ToggleThinking,
    Cancel,
    Quit,
    ModeNext,
    ModePrev,
    PermissionToggle,
    ClearMessages,
    ForceCompact,
    Audit,
    VoiceToggle,
    /// Paste image from clipboard (Ctrl+V).
    PasteImage,
    CopyLastResponse,
    BackgroundAgent,
    /// Submit a follow-up message (Tier 2) while the agent is running.
    SubmitFollowUp,
    /// Submit a post-complete message (Tier 3) while the agent is running.
    SubmitPostComplete,
    /// Expand/collapse all thinking blocks in the message list.
    ExpandThinking,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KeyBinding {
    pub code: KeyCode,
    pub modifiers: KeyModifiers,
}

impl KeyBinding {
    pub fn matches(&self, event: KeyEvent) -> bool {
        self.code == event.code && self.modifiers == event.modifiers
    }
}

#[derive(Debug, Clone)]
pub struct KeybindState {
    bindings: HashMap<Action, Vec<KeyBinding>>,
}

impl Default for KeybindState {
    fn default() -> Self {
        Self {
            bindings: default_keybinds(),
        }
    }
}

impl KeybindState {
    pub fn action_for(&self, event: KeyEvent) -> Option<Action> {
        self.bindings.iter().find_map(|(action, bindings)| {
            bindings
                .iter()
                .any(|binding| binding.matches(event))
                .then_some(*action)
        })
    }

    pub fn merge_overrides(&mut self, overrides: HashMap<Action, Vec<KeyBinding>>) {
        for (action, bindings) in overrides {
            self.bindings.insert(action, bindings);
        }
    }

    pub fn bindings(&self) -> &HashMap<Action, Vec<KeyBinding>> {
        &self.bindings
    }
}

fn ctrl(code: char) -> KeyBinding {
    KeyBinding {
        code: KeyCode::Char(code),
        modifiers: KeyModifiers::CONTROL,
    }
}

pub fn default_keybinds() -> HashMap<Action, Vec<KeyBinding>> {
    HashMap::from([
        (Action::CommandPalette, vec![ctrl('/'), ctrl('k')]),
        (Action::NewSession, vec![ctrl('n')]),
        (Action::ModelSwitch, vec![ctrl('m')]),
        (
            Action::ScrollUp,
            vec![KeyBinding {
                code: KeyCode::PageUp,
                modifiers: KeyModifiers::empty(),
            }],
        ),
        (
            Action::ScrollDown,
            vec![KeyBinding {
                code: KeyCode::PageDown,
                modifiers: KeyModifiers::empty(),
            }],
        ),
        (
            Action::ScrollTop,
            vec![KeyBinding {
                code: KeyCode::Home,
                modifiers: KeyModifiers::empty(),
            }],
        ),
        (
            Action::ScrollBottom,
            vec![KeyBinding {
                code: KeyCode::End,
                modifiers: KeyModifiers::empty(),
            }],
        ),
        (Action::ToggleSidebar, vec![ctrl('s')]),
        (Action::ToggleThinking, vec![ctrl('t')]),
        (Action::Cancel, vec![ctrl('c')]),
        (Action::SessionList, vec![ctrl('l')]),
        (Action::PasteImage, vec![ctrl('v')]),
        (Action::VoiceToggle, vec![ctrl('r')]),
        (Action::CopyLastResponse, vec![ctrl('y')]),
        (Action::BackgroundAgent, vec![ctrl('b')]),
        (
            Action::SubmitFollowUp,
            vec![KeyBinding {
                code: KeyCode::Enter,
                modifiers: KeyModifiers::ALT,
            }],
        ),
        (
            Action::SubmitPostComplete,
            vec![KeyBinding {
                code: KeyCode::Enter,
                modifiers: KeyModifiers::ALT.union(KeyModifiers::CONTROL),
            }],
        ),
        (Action::ExpandThinking, vec![ctrl('e')]),
    ])
}
