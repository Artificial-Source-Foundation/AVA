//! Agent color coding — assigns distinct colors to HQ agents for visual identification.

use std::collections::HashMap;

/// Predefined palette of 8 distinct colors for agent identification.
const PALETTE: &[&str] = &[
    "blue", "green", "amber", "purple", "cyan", "red", "pink", "teal",
];

/// Manages color assignments for HQ agents (Leads, Workers, Scouts).
///
/// Each agent name gets a consistent color. The palette wraps around when
/// more agents are spawned than colors available.
#[derive(Debug, Default)]
pub struct AgentColorManager {
    assignments: HashMap<String, String>,
    next_index: usize,
}

impl AgentColorManager {
    pub fn new() -> Self {
        Self::default()
    }

    /// Assign a color to the given agent name. Returns the same color on
    /// repeated calls with the same name.
    pub fn assign_color(&mut self, agent_name: &str) -> &str {
        if !self.assignments.contains_key(agent_name) {
            let color = PALETTE[self.next_index % PALETTE.len()].to_string();
            self.next_index += 1;
            self.assignments.insert(agent_name.to_string(), color);
        }
        &self.assignments[agent_name]
    }

    /// Look up an already-assigned color without creating a new assignment.
    pub fn get_color(&self, agent_name: &str) -> Option<&str> {
        self.assignments.get(agent_name).map(String::as_str)
    }

    /// Number of distinct agents that have been assigned colors.
    pub fn assignment_count(&self) -> usize {
        self.assignments.len()
    }

    /// Available palette size.
    pub fn palette_size(&self) -> usize {
        PALETTE.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_name_gets_same_color() {
        let mut mgr = AgentColorManager::new();
        let c1 = mgr.assign_color("Backend Lead").to_string();
        let c2 = mgr.assign_color("Backend Lead").to_string();
        assert_eq!(c1, c2);
    }

    #[test]
    fn different_names_get_different_colors() {
        let mut mgr = AgentColorManager::new();
        let c1 = mgr.assign_color("Backend Lead").to_string();
        let c2 = mgr.assign_color("QA Lead").to_string();
        assert_ne!(c1, c2);
    }

    #[test]
    fn palette_wraps_around() {
        let mut mgr = AgentColorManager::new();
        let names: Vec<String> = (0..12).map(|i| format!("agent-{i}")).collect();
        for name in &names {
            mgr.assign_color(name);
        }
        // 9th agent should wrap to the 1st color
        let color_0 = mgr.get_color("agent-0").unwrap();
        let color_8 = mgr.get_color("agent-8").unwrap();
        assert_eq!(color_0, color_8);
        assert_eq!(mgr.assignment_count(), 12);
    }

    #[test]
    fn get_color_returns_none_for_unknown() {
        let mgr = AgentColorManager::new();
        assert!(mgr.get_color("unknown").is_none());
    }

    #[test]
    fn all_palette_colors_are_assigned_in_order() {
        let mut mgr = AgentColorManager::new();
        let colors: Vec<String> = (0..8)
            .map(|i| mgr.assign_color(&format!("a{i}")).to_string())
            .collect();
        assert_eq!(
            colors,
            vec!["blue", "green", "amber", "purple", "cyan", "red", "pink", "teal"]
        );
    }
}
