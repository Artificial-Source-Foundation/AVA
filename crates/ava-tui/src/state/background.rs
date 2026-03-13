use crate::state::messages::UiMessage;
use std::sync::Arc;
use std::time::Instant;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TaskStatus {
    Running,
    Completed,
    Failed,
}

impl std::fmt::Display for TaskStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Running => write!(f, "Running"),
            Self::Completed => write!(f, "Completed"),
            Self::Failed => write!(f, "Failed"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct BackgroundTask {
    pub id: usize,
    pub goal: String,
    pub status: TaskStatus,
    pub started_at: Instant,
    pub completed_at: Option<Instant>,
    pub tokens_input: usize,
    pub tokens_output: usize,
    pub cost_usd: f64,
    pub messages: Vec<UiMessage>,
    pub error: Option<String>,
    pub worktree_path: Option<String>,
    pub branch_name: Option<String>,
}

impl BackgroundTask {
    /// Elapsed duration since start (or total if completed).
    pub fn elapsed(&self) -> std::time::Duration {
        if let Some(completed) = self.completed_at {
            completed.duration_since(self.started_at)
        } else {
            self.started_at.elapsed()
        }
    }

    /// Format elapsed duration as human-readable string.
    pub fn elapsed_display(&self) -> String {
        let secs = self.elapsed().as_secs();
        if secs >= 3600 {
            format!(
                "{}h {:02}m {:02}s",
                secs / 3600,
                (secs % 3600) / 60,
                secs % 60
            )
        } else if secs >= 60 {
            format!("{}m {:02}s", secs / 60, secs % 60)
        } else {
            format!("{}s", secs)
        }
    }

    /// Truncated goal for display (max display columns).
    pub fn goal_display(&self, max_len: usize) -> String {
        crate::text_utils::truncate_display(&self.goal, max_len)
    }
}

/// Shared state for background tasks, accessed from both the TUI tick handler
/// and background tokio tasks via `Arc<std::sync::Mutex<...>>`.
#[derive(Debug)]
pub struct BackgroundState {
    pub tasks: Vec<BackgroundTask>,
    pub next_id: usize,
    pub show_task_list: bool,
    pub viewing_task: Option<usize>,
    pub selected_index: usize,
    pub notification: Option<(String, Instant)>,
}

impl Default for BackgroundState {
    fn default() -> Self {
        Self {
            tasks: Vec::new(),
            next_id: 1,
            show_task_list: false,
            viewing_task: None,
            selected_index: 0,
            notification: None,
        }
    }
}

impl BackgroundState {
    pub fn add_task(&mut self, goal: String) -> usize {
        let id = self.next_id;
        self.next_id += 1;
        self.tasks.push(BackgroundTask {
            id,
            goal,
            status: TaskStatus::Running,
            started_at: Instant::now(),
            completed_at: None,
            tokens_input: 0,
            tokens_output: 0,
            cost_usd: 0.0,
            messages: Vec::new(),
            error: None,
            worktree_path: None,
            branch_name: None,
        });
        id
    }

    pub fn set_isolation(&mut self, id: usize, worktree_path: String, branch_name: String) {
        if let Some(task) = self.tasks.iter_mut().find(|t| t.id == id) {
            task.worktree_path = Some(worktree_path);
            task.branch_name = Some(branch_name);
        }
    }

    pub fn complete_task(&mut self, id: usize) {
        if let Some(task) = self.tasks.iter_mut().find(|t| t.id == id) {
            task.status = TaskStatus::Completed;
            task.completed_at = Some(Instant::now());
            let elapsed = task.elapsed_display();
            self.notification = Some((format!("Task #{id} completed ({elapsed})"), Instant::now()));
        }
    }

    pub fn fail_task(&mut self, id: usize, error: String) {
        if let Some(task) = self.tasks.iter_mut().find(|t| t.id == id) {
            task.status = TaskStatus::Failed;
            task.completed_at = Some(Instant::now());
            task.error = Some(error);
            let elapsed = task.elapsed_display();
            self.notification = Some((format!("Task #{id} failed ({elapsed})"), Instant::now()));
        }
    }

    pub fn running_count(&self) -> usize {
        self.tasks
            .iter()
            .filter(|t| t.status == TaskStatus::Running)
            .count()
    }

    pub fn append_message(&mut self, id: usize, msg: UiMessage) {
        if let Some(task) = self.tasks.iter_mut().find(|t| t.id == id) {
            task.messages.push(msg);
        }
    }

    pub fn add_tokens(&mut self, id: usize, input: usize, output: usize, cost: f64) {
        if let Some(task) = self.tasks.iter_mut().find(|t| t.id == id) {
            task.tokens_input += input;
            task.tokens_output += output;
            task.cost_usd += cost;
        }
    }

    /// Total tokens across all tasks.
    pub fn total_tokens(&self) -> usize {
        self.tasks
            .iter()
            .map(|t| t.tokens_input + t.tokens_output)
            .sum()
    }

    /// Total cost across all tasks.
    pub fn total_cost(&self) -> f64 {
        self.tasks.iter().map(|t| t.cost_usd).sum()
    }

    /// Check if notification has expired (5 second TTL).
    pub fn expire_notification(&mut self) {
        if let Some((_, created)) = &self.notification {
            if created.elapsed() > std::time::Duration::from_secs(5) {
                self.notification = None;
            }
        }
    }

    /// Navigate selection up.
    pub fn select_prev(&mut self) {
        if self.selected_index > 0 {
            self.selected_index -= 1;
        }
    }

    /// Navigate selection down.
    pub fn select_next(&mut self) {
        if !self.tasks.is_empty() && self.selected_index + 1 < self.tasks.len() {
            self.selected_index += 1;
        }
    }

    /// Get the selected task ID.
    pub fn selected_task_id(&self) -> Option<usize> {
        self.tasks.get(self.selected_index).map(|t| t.id)
    }
}

/// Shared handle to background state, usable from async tasks and TUI.
pub type SharedBackgroundState = Arc<std::sync::Mutex<BackgroundState>>;

pub fn new_shared() -> SharedBackgroundState {
    Arc::new(std::sync::Mutex::new(BackgroundState::default()))
}
