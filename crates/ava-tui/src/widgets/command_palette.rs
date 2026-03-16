use crate::state::keybinds::Action;
use crate::widgets::select_list::{SelectItem, SelectListState};

/// How a command palette item is executed.
#[derive(Debug, Clone)]
pub enum CommandExec {
    /// Dispatch an Action variant (modals, toggles, etc.).
    Action(Action),
    /// Execute a slash command string (e.g. "/help").
    Slash(String),
}

#[derive(Debug)]
pub struct CommandPaletteState {
    pub open: bool,
    pub list: SelectListState<CommandExec>,
}

impl Default for CommandPaletteState {
    fn default() -> Self {
        Self {
            open: false,
            list: SelectListState::new(Vec::new()),
        }
    }
}

impl CommandPaletteState {
    pub fn with_defaults() -> Self {
        let items = vec![
            // Agent
            make_item(
                "Model",
                "Ctrl+M",
                "Agent",
                CommandExec::Action(Action::ModelSwitch),
            ),
            make_item(
                "Toggle Permissions",
                "",
                "Agent",
                CommandExec::Action(Action::PermissionToggle),
            ),
            make_item(
                "Compact Context",
                "",
                "Agent",
                CommandExec::Action(Action::ForceCompact),
            ),
            // Session
            make_item(
                "New Session",
                "Ctrl+N",
                "Session",
                CommandExec::Action(Action::NewSession),
            ),
            make_item(
                "Switch Session",
                "",
                "Session",
                CommandExec::Action(Action::SessionList),
            ),
            // Chat
            make_item(
                "Clear Chat",
                "",
                "Chat",
                CommandExec::Action(Action::ClearMessages),
            ),
            // Provider
            make_item(
                "Connect Provider",
                "",
                "Provider",
                CommandExec::Slash("/connect".to_string()),
            ),
            make_item(
                "Provider Status",
                "",
                "Provider",
                CommandExec::Slash("/providers".to_string()),
            ),
            // MCP
            make_item(
                "List MCP Servers",
                "",
                "MCP",
                CommandExec::Slash("/mcp".to_string()),
            ),
            make_item(
                "Reload MCP Config",
                "",
                "MCP",
                CommandExec::Slash("/mcp reload".to_string()),
            ),
            // Info
            make_item("Help", "", "Info", CommandExec::Slash("/help".to_string())),
            // UI
            make_item(
                "Toggle Sidebar",
                "Ctrl+S",
                "UI",
                CommandExec::Action(Action::ToggleSidebar),
            ),
            // App
            make_item("Quit", "Ctrl+D", "App", CommandExec::Action(Action::Quit)),
        ];

        Self {
            open: false,
            list: SelectListState::new(items),
        }
    }
}

fn make_item(name: &str, hint: &str, category: &str, exec: CommandExec) -> SelectItem<CommandExec> {
    let detail = if hint.is_empty() {
        String::new()
    } else {
        hint.to_string()
    };
    SelectItem {
        title: name.to_string(),
        detail,
        section: Some(category.to_string()),
        status: None,
        value: exec,
        enabled: true,
    }
}
