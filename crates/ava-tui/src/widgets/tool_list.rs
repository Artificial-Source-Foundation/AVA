use ava_tools::registry::ToolSource;

use crate::widgets::select_list::{SelectItem, SelectListState};

#[derive(Debug, Clone)]
pub struct ToolListItem {
    pub name: String,
    pub description: String,
    pub source: ToolSource,
}

#[derive(Default)]
pub struct ToolListState {
    pub list: SelectListState<String>,
}

impl ToolListState {
    pub fn from_items(items: Vec<ToolListItem>) -> Self {
        let select_items = build_select_items(&items);
        Self {
            list: SelectListState::new(select_items),
        }
    }

    pub fn update_items(&mut self, items: Vec<ToolListItem>) {
        let select_items = build_select_items(&items);
        self.list.set_items(select_items);
    }
}

fn build_select_items(items: &[ToolListItem]) -> Vec<SelectItem<String>> {
    // Group by source for section headers
    let mut builtin = Vec::new();
    let mut mcp: std::collections::BTreeMap<String, Vec<&ToolListItem>> =
        std::collections::BTreeMap::new();
    let mut custom = Vec::new();

    for item in items {
        match &item.source {
            ToolSource::BuiltIn => builtin.push(item),
            ToolSource::MCP { server } => mcp.entry(server.clone()).or_default().push(item),
            ToolSource::Custom { .. } => custom.push(item),
        }
    }

    let mut select_items = Vec::new();

    for item in &builtin {
        select_items.push(tool_to_select_item(item, "Core"));
    }

    for (server, tools) in &mcp {
        let section = format!("MCP: {server}");
        for item in tools {
            select_items.push(tool_to_select_item(item, &section));
        }
    }

    for item in &custom {
        select_items.push(tool_to_select_item(item, "Custom"));
    }

    select_items
}

fn tool_to_select_item(item: &ToolListItem, section: &str) -> SelectItem<String> {
    // Show source badge + truncated description as detail
    let badge = match &item.source {
        ToolSource::BuiltIn => "[Built-in]".to_string(),
        ToolSource::MCP { server } => format!("[MCP:{server}]"),
        ToolSource::Custom { .. } => "[Custom]".to_string(),
    };
    let desc = crate::text_utils::truncate_display(&item.description, 40);
    let detail = if desc.is_empty() {
        badge
    } else {
        format!("{badge} {desc}")
    };

    SelectItem {
        title: item.name.clone(),
        detail,
        section: Some(section.to_string()),
        status: None,
        value: item.name.clone(),
        enabled: true,
    }
}
