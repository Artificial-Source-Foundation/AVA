//! Routing — task domain mapping and dependency resolution.

use std::collections::{HashMap, HashSet};

use ava_types::{AvaError, Result};

use crate::plan::HqTask;
use crate::{Domain, TaskType};

/// Group tasks into waves based on their dependency graph.
///
/// - Wave 0: tasks with no dependencies (run in parallel)
/// - Wave 1: tasks whose dependencies are all in wave 0 (run in parallel)
/// - etc.
///
/// Returns an error if a dependency cycle is detected or a dependency references
/// an unknown task ID.
pub fn topological_sort(tasks: &[HqTask]) -> Result<Vec<Vec<&HqTask>>> {
    if tasks.is_empty() {
        return Ok(Vec::new());
    }

    let task_map: HashMap<&str, &HqTask> = tasks.iter().map(|t| (t.id.as_str(), t)).collect();

    // Validate all dependencies reference existing tasks
    for task in tasks {
        for dep in &task.dependencies {
            if !task_map.contains_key(dep.as_str()) {
                return Err(AvaError::ToolError(format!(
                    "task '{}' depends on unknown task id: {dep}",
                    task.id
                )));
            }
        }
    }

    let mut waves: Vec<Vec<&HqTask>> = Vec::new();
    let mut assigned: HashSet<&str> = HashSet::new();
    let mut remaining: Vec<&HqTask> = tasks.iter().collect();

    while !remaining.is_empty() {
        let wave: Vec<&HqTask> = remaining
            .iter()
            .filter(|t| {
                t.dependencies
                    .iter()
                    .all(|dep| assigned.contains(dep.as_str()))
            })
            .copied()
            .collect();

        if wave.is_empty() {
            // No tasks can be scheduled — cycle detected
            let stuck: Vec<&str> = remaining.iter().map(|t| t.id.as_str()).collect();
            return Err(AvaError::ToolError(format!(
                "dependency cycle detected among tasks: {stuck:?}"
            )));
        }

        for t in &wave {
            assigned.insert(&t.id);
        }

        let wave_ids: HashSet<&str> = wave.iter().map(|t| t.id.as_str()).collect();
        remaining.retain(|t| !wave_ids.contains(t.id.as_str()));

        waves.push(wave);
    }

    Ok(waves)
}

/// Map a domain to the most appropriate TaskType.
pub fn domain_to_task_type(domain: &Domain) -> TaskType {
    match domain {
        Domain::Frontend | Domain::Backend | Domain::DevOps => TaskType::CodeGeneration,
        Domain::QA => TaskType::Testing,
        Domain::Research => TaskType::Research,
        Domain::Debug => TaskType::Debug,
        Domain::Fullstack => TaskType::Simple,
    }
}

/// Derive a short display name for a board member from the model name.
///
/// Examples: "claude-opus-4" -> "Opus", "gpt-5.4" -> "GPT", "gemini-2.0-pro" -> "Gemini"
pub fn derive_board_name(model: &str) -> String {
    let lower = model.to_lowercase();
    if lower.contains("opus") {
        "Opus (Board)".to_string()
    } else if lower.contains("sonnet") {
        "Sonnet (Board)".to_string()
    } else if lower.contains("gemini") {
        "Gemini (Board)".to_string()
    } else if lower.contains("gpt") {
        "GPT (Board)".to_string()
    } else if lower.contains("mercury") {
        "Mercury (Board)".to_string()
    } else if lower.contains("haiku") {
        "Haiku (Board)".to_string()
    } else {
        // Use the model name itself, capitalised, with (Board) suffix
        let name = model
            .split('/')
            .next_back()
            .unwrap_or(model)
            .split('-')
            .next()
            .unwrap_or(model);
        let capitalised = {
            let mut c = name.chars();
            match c.next() {
                Some(first) => first.to_uppercase().collect::<String>() + c.as_str(),
                None => model.to_string(),
            }
        };
        format!("{capitalised} (Board)")
    }
}
