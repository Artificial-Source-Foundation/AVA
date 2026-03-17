//! Cron-based task scheduler for recurring agent tasks.
//!
//! Provides a simple scheduler that stores tasks with cron expressions
//! and can report when tasks should next run.

use std::collections::HashMap;

/// A scheduled task with a cron expression and command.
#[derive(Debug, Clone)]
pub struct ScheduledTask {
    /// Human-readable task name.
    pub name: String,
    /// Cron expression (e.g., "0 * * * *" for hourly).
    pub cron_expression: String,
    /// Command or prompt to execute when the task fires.
    pub command: String,
    /// Whether the task is currently active.
    pub enabled: bool,
}

/// Parsed components of a cron expression.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CronFields {
    pub minute: String,
    pub hour: String,
    pub day_of_month: String,
    pub month: String,
    pub day_of_week: String,
}

/// Errors from cron expression parsing.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CronParseError {
    /// Wrong number of fields (expected 5).
    InvalidFieldCount(usize),
    /// A field contains invalid characters.
    InvalidField(String),
}

impl std::fmt::Display for CronParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            CronParseError::InvalidFieldCount(n) => {
                write!(f, "expected 5 cron fields, got {}", n)
            }
            CronParseError::InvalidField(field) => {
                write!(f, "invalid cron field: '{}'", field)
            }
        }
    }
}

/// Validate and parse a cron expression into its five fields.
///
/// Accepts standard 5-field cron format: `minute hour day-of-month month day-of-week`.
/// Each field may contain digits, `*`, `-`, `/`, or `,`.
pub fn parse_cron(expression: &str) -> Result<CronFields, CronParseError> {
    let fields: Vec<&str> = expression.split_whitespace().collect();
    if fields.len() != 5 {
        return Err(CronParseError::InvalidFieldCount(fields.len()));
    }

    for field in &fields {
        if !is_valid_cron_field(field) {
            return Err(CronParseError::InvalidField(field.to_string()));
        }
    }

    Ok(CronFields {
        minute: fields[0].to_string(),
        hour: fields[1].to_string(),
        day_of_month: fields[2].to_string(),
        month: fields[3].to_string(),
        day_of_week: fields[4].to_string(),
    })
}

/// Check whether a single cron field is syntactically valid.
fn is_valid_cron_field(field: &str) -> bool {
    if field.is_empty() {
        return false;
    }
    field
        .chars()
        .all(|c| c.is_ascii_digit() || matches!(c, '*' | '-' | '/' | ','))
}

/// Task scheduler that manages a collection of scheduled tasks.
#[derive(Debug, Default)]
pub struct TaskScheduler {
    tasks: HashMap<String, ScheduledTask>,
}

impl TaskScheduler {
    /// Create a new empty scheduler.
    pub fn new() -> Self {
        Self {
            tasks: HashMap::new(),
        }
    }

    /// Add a task to the scheduler. Returns an error if the cron expression is invalid.
    pub fn add_task(&mut self, task: ScheduledTask) -> Result<(), CronParseError> {
        // Validate the cron expression before accepting the task
        parse_cron(&task.cron_expression)?;
        self.tasks.insert(task.name.clone(), task);
        Ok(())
    }

    /// Remove a task by name. Returns `true` if the task existed.
    pub fn remove_task(&mut self, name: &str) -> bool {
        self.tasks.remove(name).is_some()
    }

    /// List all scheduled tasks.
    pub fn list_tasks(&self) -> Vec<&ScheduledTask> {
        self.tasks.values().collect()
    }

    /// Parse the cron expression for a named task and return a human-readable
    /// description of when it next runs. Returns `None` if the task doesn't exist.
    pub fn next_run_time(&self, name: &str) -> Option<String> {
        let task = self.tasks.get(name)?;
        if !task.enabled {
            return Some("disabled".to_string());
        }
        let fields = parse_cron(&task.cron_expression).ok()?;
        Some(format!(
            "next run: minute={} hour={} dom={} month={} dow={}",
            fields.minute, fields.hour, fields.day_of_month, fields.month, fields.day_of_week
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_valid_cron_expression() {
        let fields = parse_cron("0 * * * *").unwrap();
        assert_eq!(fields.minute, "0");
        assert_eq!(fields.hour, "*");
    }

    #[test]
    fn parse_invalid_cron_field_count() {
        let err = parse_cron("0 * *").unwrap_err();
        assert_eq!(err, CronParseError::InvalidFieldCount(3));
    }

    #[test]
    fn add_and_list_tasks() {
        let mut scheduler = TaskScheduler::new();
        scheduler
            .add_task(ScheduledTask {
                name: "hourly-check".to_string(),
                cron_expression: "0 * * * *".to_string(),
                command: "cargo test".to_string(),
                enabled: true,
            })
            .unwrap();
        assert_eq!(scheduler.list_tasks().len(), 1);
    }

    #[test]
    fn remove_task_returns_true_when_exists() {
        let mut scheduler = TaskScheduler::new();
        scheduler
            .add_task(ScheduledTask {
                name: "test".to_string(),
                cron_expression: "*/5 * * * *".to_string(),
                command: "echo hello".to_string(),
                enabled: true,
            })
            .unwrap();
        assert!(scheduler.remove_task("test"));
        assert!(!scheduler.remove_task("test"));
    }
}
