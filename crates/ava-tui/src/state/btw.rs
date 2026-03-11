use std::sync::{Arc, Mutex};

/// State for the `/btw` side-conversation overlay.
///
/// This is an ephemeral Q&A that runs while the agent is working.
/// The question and answer never enter message history.
#[derive(Default)]
pub struct BtwState {
    /// True while the LLM call is in flight.
    pub pending: bool,
    /// The completed response to display in the overlay.
    pub response: Option<BtwResponse>,
    /// Shared slot for the background task to deposit its result.
    /// Polled on each tick by the event loop.
    pub pending_result: Option<Arc<Mutex<Option<BtwResponse>>>>,
}

/// A completed `/btw` side question and its answer.
#[derive(Debug, Clone)]
pub struct BtwResponse {
    pub question: String,
    pub answer: String,
}
