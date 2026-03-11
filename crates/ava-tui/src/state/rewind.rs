/// Rewind system — allows users to revert conversation and/or file changes
/// to any previous checkpoint (created at each user message submission).

/// Tracks what kind of change was made to a file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ChangeType {
    Created,
    Modified,
    Deleted,
}

/// A single file change recorded during an agent turn.
#[derive(Debug, Clone)]
pub struct FileChange {
    /// Absolute path to the file.
    pub path: String,
    /// Original content before the change (None if the file was created).
    pub original_content: Option<String>,
    /// What kind of change was made.
    pub change_type: ChangeType,
}

/// A checkpoint created when a user submits a message.
/// Stores the message index and any file changes that occurred
/// between this checkpoint and the next.
#[derive(Debug, Clone)]
pub struct Checkpoint {
    /// Index of the user message in the UI message list.
    pub message_index: usize,
    /// Preview of the user message (first 80 chars).
    pub message_preview: String,
    /// ISO timestamp when the checkpoint was created.
    pub timestamp: String,
    /// File changes that occurred during the agent turn following this message.
    pub file_changes: Vec<FileChange>,
}

/// The 5 rewind options presented to the user.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RewindOption {
    RestoreCodeAndConversation,
    RestoreConversation,
    RestoreCode,
    SummarizeFromHere,
    Cancel,
}

impl RewindOption {
    pub const ALL: [RewindOption; 5] = [
        RewindOption::RestoreCodeAndConversation,
        RewindOption::RestoreConversation,
        RewindOption::RestoreCode,
        RewindOption::SummarizeFromHere,
        RewindOption::Cancel,
    ];

    pub fn label(&self) -> &'static str {
        match self {
            RewindOption::RestoreCodeAndConversation => "Restore code and conversation",
            RewindOption::RestoreConversation => "Restore conversation only",
            RewindOption::RestoreCode => "Restore code only",
            RewindOption::SummarizeFromHere => "Summarize from here",
            RewindOption::Cancel => "Never mind",
        }
    }

    pub fn description(&self) -> &'static str {
        match self {
            RewindOption::RestoreCodeAndConversation => {
                "Revert files and remove messages after this point"
            }
            RewindOption::RestoreConversation => {
                "Remove messages after this point, keep file changes"
            }
            RewindOption::RestoreCode => {
                "Revert files to their state before this point, keep conversation"
            }
            RewindOption::SummarizeFromHere => {
                "Compact everything before this point into a summary"
            }
            RewindOption::Cancel => "Close this modal",
        }
    }
}

/// State for the rewind modal and checkpoint tracking.
#[derive(Debug, Clone)]
pub struct RewindState {
    /// All checkpoints created during this session.
    pub checkpoints: Vec<Checkpoint>,
    /// Whether the rewind modal is currently showing.
    pub active: bool,
    /// Which of the 5 options is currently selected (0-4).
    pub selected_option: usize,
}

impl Default for RewindState {
    fn default() -> Self {
        Self {
            checkpoints: Vec::new(),
            active: false,
            selected_option: 0,
        }
    }
}

impl RewindState {
    /// Create a new checkpoint for a user message.
    pub fn create_checkpoint(&mut self, message_index: usize, message_content: &str) {
        let preview = if message_content.len() > 80 {
            format!("{}...", &message_content[..77])
        } else {
            message_content.to_string()
        };
        // Replace newlines with spaces for the preview
        let preview = preview.replace('\n', " ");

        self.checkpoints.push(Checkpoint {
            message_index,
            message_preview: preview,
            timestamp: chrono::Local::now().format("%H:%M:%S").to_string(),
            file_changes: Vec::new(),
        });
    }

    /// Record a file change on the current (latest) checkpoint.
    pub fn record_file_change(&mut self, change: FileChange) {
        if let Some(checkpoint) = self.checkpoints.last_mut() {
            // If we already have a change for this path, update it
            // (keep the original_content from the first change)
            if let Some(existing) = checkpoint
                .file_changes
                .iter_mut()
                .find(|c| c.path == change.path)
            {
                // Only update change_type — keep original_content from first snapshot
                existing.change_type = change.change_type;
            } else {
                checkpoint.file_changes.push(change);
            }
        }
    }

    /// Get the latest checkpoint (the one we'd rewind to).
    pub fn latest_checkpoint(&self) -> Option<&Checkpoint> {
        self.checkpoints.last()
    }

    /// Open the rewind modal.
    pub fn open(&mut self) {
        self.active = true;
        self.selected_option = 0;
    }

    /// Close the rewind modal.
    pub fn close(&mut self) {
        self.active = false;
        self.selected_option = 0;
    }

    /// Move selection up.
    pub fn select_prev(&mut self) {
        self.selected_option = self.selected_option.saturating_sub(1);
    }

    /// Move selection down.
    pub fn select_next(&mut self) {
        if self.selected_option + 1 < RewindOption::ALL.len() {
            self.selected_option += 1;
        }
    }

    /// Get the currently selected option.
    pub fn selected(&self) -> RewindOption {
        RewindOption::ALL[self.selected_option]
    }

    /// Restore files changed after the given checkpoint index.
    /// Returns the number of files restored.
    pub fn restore_files_after(&self, checkpoint_index: usize) -> (usize, Vec<String>) {
        let mut restored = 0;
        let mut errors = Vec::new();

        for checkpoint in &self.checkpoints[checkpoint_index..] {
            for change in &checkpoint.file_changes {
                let result = match change.change_type {
                    ChangeType::Created => {
                        // File was created — delete it
                        std::fs::remove_file(&change.path)
                    }
                    ChangeType::Modified => {
                        // File was modified — restore original content
                        if let Some(ref content) = change.original_content {
                            std::fs::write(&change.path, content)
                        } else {
                            continue;
                        }
                    }
                    ChangeType::Deleted => {
                        // File was deleted — restore it
                        if let Some(ref content) = change.original_content {
                            std::fs::write(&change.path, content)
                        } else {
                            continue;
                        }
                    }
                };

                match result {
                    Ok(()) => restored += 1,
                    Err(e) => errors.push(format!("{}: {e}", change.path)),
                }
            }
        }

        (restored, errors)
    }

    /// Count total file changes after a given checkpoint index.
    pub fn file_change_count_after(&self, checkpoint_index: usize) -> usize {
        self.checkpoints[checkpoint_index..]
            .iter()
            .map(|c| c.file_changes.len())
            .sum()
    }

    /// Remove checkpoints after a given index (inclusive).
    pub fn truncate_after(&mut self, checkpoint_index: usize) {
        self.checkpoints.truncate(checkpoint_index);
    }
}

/// Snapshot a file's content before a tool modifies it.
/// Returns `None` if the file doesn't exist (meaning a write would create it).
pub fn snapshot_file(path: &str) -> Option<String> {
    std::fs::read_to_string(path).ok()
}
