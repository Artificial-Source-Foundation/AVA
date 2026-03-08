use crate::state::keybinds::Action;
use nucleo::pattern::{CaseMatching, Normalization, Pattern};
use nucleo::Matcher;

/// How a command palette item is executed.
#[derive(Debug, Clone)]
pub enum CommandExec {
    /// Dispatch an Action variant (modals, toggles, etc.).
    Action(Action),
    /// Execute a slash command string (e.g. "/status").
    Slash(String),
}

#[derive(Debug, Clone)]
pub struct CommandItem {
    pub exec: CommandExec,
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
            // Agent
            CommandItem {
                exec: CommandExec::Action(Action::ModelSwitch),
                name: "Model".to_string(),
                category: "Agent".to_string(),
                hint: "Ctrl+M".to_string(),
            },
            CommandItem {
                exec: CommandExec::Action(Action::YoloToggle),
                name: "Toggle YOLO".to_string(),
                category: "Agent".to_string(),
                hint: "Ctrl+Y".to_string(),
            },
            CommandItem {
                exec: CommandExec::Action(Action::ForceCompact),
                name: "Compact Context".to_string(),
                category: "Agent".to_string(),
                hint: String::new(),
            },
            // Session
            CommandItem {
                exec: CommandExec::Action(Action::NewSession),
                name: "New Session".to_string(),
                category: "Session".to_string(),
                hint: "Ctrl+N".to_string(),
            },
            CommandItem {
                exec: CommandExec::Action(Action::SessionList),
                name: "Switch Session".to_string(),
                category: "Session".to_string(),
                hint: String::new(),
            },
            // Chat
            CommandItem {
                exec: CommandExec::Action(Action::ClearMessages),
                name: "Clear Chat".to_string(),
                category: "Chat".to_string(),
                hint: String::new(),
            },
            // Provider
            CommandItem {
                exec: CommandExec::Slash("/connect".to_string()),
                name: "Connect Provider".to_string(),
                category: "Provider".to_string(),
                hint: String::new(),
            },
            CommandItem {
                exec: CommandExec::Slash("/providers".to_string()),
                name: "Provider Status".to_string(),
                category: "Provider".to_string(),
                hint: String::new(),
            },
            // Tools
            CommandItem {
                exec: CommandExec::Slash("/tools".to_string()),
                name: "List Tools".to_string(),
                category: "Tools".to_string(),
                hint: String::new(),
            },
            CommandItem {
                exec: CommandExec::Slash("/tools reload".to_string()),
                name: "Reload Tools".to_string(),
                category: "Tools".to_string(),
                hint: String::new(),
            },
            // MCP
            CommandItem {
                exec: CommandExec::Slash("/mcp".to_string()),
                name: "List MCP Servers".to_string(),
                category: "MCP".to_string(),
                hint: String::new(),
            },
            CommandItem {
                exec: CommandExec::Slash("/mcp reload".to_string()),
                name: "Reload MCP Config".to_string(),
                category: "MCP".to_string(),
                hint: String::new(),
            },
            // Info
            CommandItem {
                exec: CommandExec::Slash("/status".to_string()),
                name: "Status".to_string(),
                category: "Info".to_string(),
                hint: String::new(),
            },
            CommandItem {
                exec: CommandExec::Slash("/diff".to_string()),
                name: "Git Diff".to_string(),
                category: "Info".to_string(),
                hint: String::new(),
            },
            CommandItem {
                exec: CommandExec::Slash("/help".to_string()),
                name: "Help".to_string(),
                category: "Info".to_string(),
                hint: String::new(),
            },
            // Security
            CommandItem {
                exec: CommandExec::Action(Action::Audit),
                name: "Audit Log".to_string(),
                category: "Security".to_string(),
                hint: String::new(),
            },
            // UI
            CommandItem {
                exec: CommandExec::Action(Action::ToggleSidebar),
                name: "Toggle Sidebar".to_string(),
                category: "UI".to_string(),
                hint: "Ctrl+S".to_string(),
            },
            // App
            CommandItem {
                exec: CommandExec::Action(Action::Quit),
                name: "Quit".to_string(),
                category: "App".to_string(),
                hint: "Ctrl+D".to_string(),
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
