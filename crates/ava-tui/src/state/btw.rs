use crate::state::messages::UiMessage;

/// State for the `/btw` conversation branch.
///
/// Works like `git stash` for chat: saves a checkpoint of the current
/// conversation, lets the user ask questions in the normal chat flow
/// (with full tool use), then restores the checkpoint on `/btw end`
/// or Ctrl+Z — discarding all btw messages.
#[derive(Default)]
pub struct BtwState {
    /// True while in a btw branch.
    pub active: bool,
    /// Saved messages from before the branch started.
    pub checkpoint_messages: Vec<UiMessage>,
    /// Saved scroll offset from before the branch started.
    pub checkpoint_scroll: u16,
}
