use crate::state::keybinds::Action;
use nucleo::pattern::{CaseMatching, Normalization, Pattern};
use nucleo::Matcher;

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
                action: Action::ModelSwitch,
                name: "Model".to_string(),
                category: "Agent".to_string(),
                hint: "Ctrl+M".to_string(),
            },
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
            CommandItem {
                action: Action::ClearMessages,
                name: "Clear Chat".to_string(),
                category: "Chat".to_string(),
                hint: String::new(),
            },
            CommandItem {
                action: Action::Quit,
                name: "Quit".to_string(),
                category: "App".to_string(),
                hint: "Ctrl+D".to_string(),
            },
            CommandItem {
                action: Action::ToggleSidebar,
                name: "Toggle Sidebar".to_string(),
                category: "UI".to_string(),
                hint: "Ctrl+S".to_string(),
            },
            CommandItem {
                action: Action::ForceCompact,
                name: "Compact Context".to_string(),
                category: "Agent".to_string(),
                hint: String::new(),
            },
            CommandItem {
                action: Action::Audit,
                name: "Audit Log".to_string(),
                category: "Security".to_string(),
                hint: String::new(),
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
        let mut matcher = Matcher::new(nucleo::Config::DEFAULT);
        let needle = Pattern::parse(
            &self.query,
            CaseMatching::Ignore,
            Normalization::Smart,
        );
        let mut items: Vec<_> = self
            .items
            .iter()
            .filter_map(|item| {
                let mut buf = Vec::new();
                let haystack = nucleo::Utf32Str::new(&item.name, &mut buf);
                needle.score(haystack, &mut matcher).map(|score| (score, item))
            })
            .collect();
        items.sort_by(|a, b| b.0.cmp(&a.0));
        items.into_iter().map(|(_, item)| item).collect()
    }
}
