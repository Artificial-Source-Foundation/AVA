use std::sync::Arc;
use std::time::Duration;

use serde::Serialize;
use tokio::sync::{oneshot, Mutex};
use uuid::Uuid;

pub const DEFAULT_INTERACTIVE_REQUEST_TIMEOUT: Duration = Duration::from_secs(5 * 60);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum InteractiveRequestKind {
    Approval,
    Question,
    Plan,
}

impl InteractiveRequestKind {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::Approval => "approval",
            Self::Question => "question",
            Self::Plan => "plan",
        }
    }

    pub const fn request_id_prefix(self) -> &'static str {
        self.as_str()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum InteractiveRequestPhase {
    Pending,
    Resolved,
    TimedOut,
    Cancelled,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InteractiveTimeoutPolicy {
    approval: Duration,
    question: Duration,
    plan: Duration,
}

impl InteractiveTimeoutPolicy {
    pub const fn new(approval: Duration, question: Duration, plan: Duration) -> Self {
        Self {
            approval,
            question,
            plan,
        }
    }

    pub const fn timeout_for(self, kind: InteractiveRequestKind) -> Duration {
        match kind {
            InteractiveRequestKind::Approval => self.approval,
            InteractiveRequestKind::Question => self.question,
            InteractiveRequestKind::Plan => self.plan,
        }
    }
}

impl Default for InteractiveTimeoutPolicy {
    fn default() -> Self {
        Self::new(
            DEFAULT_INTERACTIVE_REQUEST_TIMEOUT,
            DEFAULT_INTERACTIVE_REQUEST_TIMEOUT,
            DEFAULT_INTERACTIVE_REQUEST_TIMEOUT,
        )
    }
}

pub const fn canonical_interactive_timeout_policy() -> InteractiveTimeoutPolicy {
    InteractiveTimeoutPolicy::new(
        DEFAULT_INTERACTIVE_REQUEST_TIMEOUT,
        DEFAULT_INTERACTIVE_REQUEST_TIMEOUT,
        DEFAULT_INTERACTIVE_REQUEST_TIMEOUT,
    )
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InteractiveRequestHandle {
    pub request_id: String,
    pub kind: InteractiveRequestKind,
    pub phase: InteractiveRequestPhase,
}

impl InteractiveRequestHandle {
    fn pending(kind: InteractiveRequestKind) -> Self {
        Self {
            request_id: format!("{}-{}", kind.request_id_prefix(), Uuid::new_v4()),
            kind,
            phase: InteractiveRequestPhase::Pending,
        }
    }

    fn into_terminal(mut self, phase: InteractiveRequestPhase) -> Self {
        self.phase = phase;
        self
    }
}

struct PendingInteractiveRequest<T> {
    handle: InteractiveRequestHandle,
    reply: oneshot::Sender<T>,
}

#[derive(Debug)]
pub struct TerminalInteractiveRequest<T> {
    pub handle: InteractiveRequestHandle,
    pub reply: oneshot::Sender<T>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ResolveInteractiveRequestError {
    MissingPendingRequest {
        kind: InteractiveRequestKind,
    },
    StaleRequestId {
        kind: InteractiveRequestKind,
        request_id: String,
        current_request_id: String,
    },
}

#[derive(Clone)]
pub struct InteractiveRequestStore<T> {
    kind: InteractiveRequestKind,
    timeout_policy: InteractiveTimeoutPolicy,
    pending: Arc<Mutex<Option<PendingInteractiveRequest<T>>>>,
}

impl<T> InteractiveRequestStore<T> {
    pub fn new(kind: InteractiveRequestKind) -> Self {
        Self::with_timeout_policy(kind, canonical_interactive_timeout_policy())
    }

    pub fn with_timeout_policy(
        kind: InteractiveRequestKind,
        timeout_policy: InteractiveTimeoutPolicy,
    ) -> Self {
        Self {
            kind,
            timeout_policy,
            pending: Arc::new(Mutex::new(None)),
        }
    }

    pub const fn kind(&self) -> InteractiveRequestKind {
        self.kind
    }

    pub const fn timeout(&self) -> Duration {
        self.timeout_policy.timeout_for(self.kind)
    }

    pub async fn register(&self, reply: oneshot::Sender<T>) -> InteractiveRequestHandle {
        let handle = InteractiveRequestHandle::pending(self.kind);
        *self.pending.lock().await = Some(PendingInteractiveRequest {
            handle: handle.clone(),
            reply,
        });
        handle
    }

    pub async fn resolve(
        &self,
        request_id: Option<&str>,
    ) -> Result<TerminalInteractiveRequest<T>, ResolveInteractiveRequestError> {
        let mut pending = self.pending.lock().await;
        match pending.take() {
            Some(entry) => {
                if request_id.is_none_or(|expected| entry.handle.request_id == expected) {
                    Ok(TerminalInteractiveRequest {
                        handle: entry
                            .handle
                            .into_terminal(InteractiveRequestPhase::Resolved),
                        reply: entry.reply,
                    })
                } else {
                    let current_request_id = entry.handle.request_id.clone();
                    *pending = Some(entry);
                    Err(ResolveInteractiveRequestError::StaleRequestId {
                        kind: self.kind,
                        request_id: request_id.expect("request_id checked above").to_string(),
                        current_request_id,
                    })
                }
            }
            None => Err(ResolveInteractiveRequestError::MissingPendingRequest { kind: self.kind }),
        }
    }

    pub async fn timeout_request(&self, request_id: &str) -> Option<TerminalInteractiveRequest<T>> {
        self.take_terminal(request_id, InteractiveRequestPhase::TimedOut)
            .await
    }

    pub async fn cancel_pending(&self) -> Option<TerminalInteractiveRequest<T>> {
        let mut pending = self.pending.lock().await;
        pending.take().map(|entry| TerminalInteractiveRequest {
            handle: entry
                .handle
                .into_terminal(InteractiveRequestPhase::Cancelled),
            reply: entry.reply,
        })
    }

    pub async fn current_request_id(&self) -> Option<String> {
        self.pending
            .lock()
            .await
            .as_ref()
            .map(|entry| entry.handle.request_id.clone())
    }

    async fn take_terminal(
        &self,
        request_id: &str,
        phase: InteractiveRequestPhase,
    ) -> Option<TerminalInteractiveRequest<T>> {
        let mut pending = self.pending.lock().await;
        if pending
            .as_ref()
            .is_some_and(|entry| entry.handle.request_id == request_id)
        {
            pending.take().map(|entry| TerminalInteractiveRequest {
                handle: entry.handle.into_terminal(phase),
                reply: entry.reply,
            })
        } else {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::sync::oneshot;

    #[tokio::test]
    async fn request_ids_are_kind_prefixed_and_correlatable() {
        let approval = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Approval);
        let question = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Question);

        let (approval_tx, _approval_rx) = oneshot::channel();
        let (question_tx, _question_rx) = oneshot::channel();

        let approval_handle = approval.register(approval_tx).await;
        let question_handle = question.register(question_tx).await;

        assert!(approval_handle.request_id.starts_with("approval-"));
        assert!(question_handle.request_id.starts_with("question-"));
        assert_ne!(approval_handle.request_id, question_handle.request_id);
        assert_eq!(
            approval.current_request_id().await,
            Some(approval_handle.request_id)
        );
    }

    #[tokio::test]
    async fn stale_request_ids_are_rejected_without_consuming_current_request() {
        let store = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Plan);
        let (tx, _rx) = oneshot::channel();
        let handle = store.register(tx).await;

        let error = store
            .resolve(Some("plan-stale"))
            .await
            .expect_err("stale request should fail");

        assert_eq!(
            error,
            ResolveInteractiveRequestError::StaleRequestId {
                kind: InteractiveRequestKind::Plan,
                request_id: "plan-stale".to_string(),
                current_request_id: handle.request_id.clone(),
            }
        );
        assert_eq!(
            store.current_request_id().await,
            Some(handle.request_id.clone())
        );

        let resolved = store
            .resolve(Some(&handle.request_id))
            .await
            .expect("current request should resolve");
        assert_eq!(resolved.handle.phase, InteractiveRequestPhase::Resolved);
        assert_eq!(resolved.handle.request_id, handle.request_id);
        assert!(store.current_request_id().await.is_none());
    }

    #[tokio::test]
    async fn timeout_only_consumes_matching_current_request() {
        let store = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Question);
        let (tx, _rx) = oneshot::channel();
        let handle = store.register(tx).await;

        assert!(store.timeout_request("question-stale").await.is_none());
        assert_eq!(
            store.current_request_id().await,
            Some(handle.request_id.clone())
        );

        let timed_out = store
            .timeout_request(&handle.request_id)
            .await
            .expect("matching request should time out");
        assert_eq!(timed_out.handle.phase, InteractiveRequestPhase::TimedOut);
        assert_eq!(timed_out.handle.request_id, handle.request_id);
        assert!(store.current_request_id().await.is_none());
    }

    #[tokio::test]
    async fn cancel_cleanup_clears_pending_request() {
        let store = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Approval);
        let (tx, _rx) = oneshot::channel();
        let handle = store.register(tx).await;

        let cancelled = store
            .cancel_pending()
            .await
            .expect("pending request should be cancelled");
        assert_eq!(cancelled.handle.phase, InteractiveRequestPhase::Cancelled);
        assert_eq!(cancelled.handle.request_id, handle.request_id);
        assert!(store.current_request_id().await.is_none());
    }

    #[test]
    fn canonical_timeout_policy_is_shared_across_request_kinds() {
        let policy = canonical_interactive_timeout_policy();
        assert_eq!(
            policy.timeout_for(InteractiveRequestKind::Approval),
            DEFAULT_INTERACTIVE_REQUEST_TIMEOUT
        );
        assert_eq!(
            policy.timeout_for(InteractiveRequestKind::Question),
            DEFAULT_INTERACTIVE_REQUEST_TIMEOUT
        );
        assert_eq!(
            policy.timeout_for(InteractiveRequestKind::Plan),
            DEFAULT_INTERACTIVE_REQUEST_TIMEOUT
        );
    }
}
