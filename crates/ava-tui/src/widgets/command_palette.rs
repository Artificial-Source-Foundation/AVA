use crate::state::keybinds::Action;

#[derive(Debug, Clone)]
pub struct CommandItem {
    pub action: Action,
    pub name: String,
    pub category: String,
    pub hint: String,
}

#[derive(Debug, Default)]
pub struct CommandPaletteState {
    pub open: bool,
    pub query: String,
    pub selected: usize,
    pub items: Vec<CommandItem>,
}

impl CommandPaletteState {
    pub fn with_defaults() -> Self {
        let items = vec![
            CommandItem {
                action: Action::NewSession,
                name: "New Session".to_string(),
                category: "Session".to_string(),
                hint: "Ctrl+N".to_string(),
            },
            CommandItem {
                action: Action::SessionList,
                name: "Switch Session".to_string(),
                category: "Session".to_string(),
                hint: "Ctrl+K".to_string(),
            },
            CommandItem {
                action: Action::YoloToggle,
                name: "Toggle YOLO".to_string(),
                category: "Agent".to_string(),
                hint: "Ctrl+Y".to_string(),
            },
        ];

        Self {
            items,
            ..Self::default()
        }
    }

    pub fn filtered(&self) -> Vec<&CommandItem> {
        if self.query.trim().is_empty() {
            return self.items.iter().collect();
        }
        let needle = self.query.to_lowercase();
        self.items
            .iter()
            .filter(|item| item.name.to_lowercase().contains(&needle))
            .collect()
    }
}
