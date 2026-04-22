//! Shared interactive request lifecycle primitives.
//!
//! Ownership: approval/question/plan request IDs, queueing, resolve/cancel/timeout
//! transitions, and timeout policy used by all surface adapters.

use std::collections::{HashMap, VecDeque};
use std::sync::Arc;
use std::time::Duration;

use serde::Serialize;
use tokio::sync::{oneshot, Mutex};
use uuid::Uuid;

pub const DEFAULT_INTERACTIVE_REQUEST_TIMEOUT: Duration = Duration::from_secs(5 * 60);
const MIN_TIMEOUT_WATCHDOG_POLL_INTERVAL: Duration = Duration::from_millis(1);
const MAX_TIMEOUT_WATCHDOG_POLL_INTERVAL: Duration = Duration::from_secs(1);

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
    #[serde(skip_serializing_if = "Option::is_none")]
    pub run_id: Option<String>,
}

impl InteractiveRequestHandle {
    fn pending(kind: InteractiveRequestKind, run_id: Option<String>) -> Self {
        Self {
            request_id: format!("{}-{}", kind.request_id_prefix(), Uuid::new_v4()),
            kind,
            phase: InteractiveRequestPhase::Pending,
            run_id,
        }
    }

    fn into_terminal(mut self, phase: InteractiveRequestPhase) -> Self {
        self.phase = phase;
        self
    }
}

struct PendingInteractiveRequest<T> {
    seq: u64,
    handle: InteractiveRequestHandle,
    reply: oneshot::Sender<T>,
}

struct PendingInteractiveQueues<T> {
    next_seq: u64,
    by_owner: HashMap<String, VecDeque<PendingInteractiveRequest<T>>>,
}

impl<T> Default for PendingInteractiveQueues<T> {
    fn default() -> Self {
        Self {
            next_seq: 0,
            by_owner: HashMap::new(),
        }
    }
}

impl<T> PendingInteractiveQueues<T> {
    fn global_front(&self) -> Option<(&str, &PendingInteractiveRequest<T>)> {
        self.by_owner
            .iter()
            .filter_map(|(owner, queue)| queue.front().map(|entry| (owner.as_str(), entry)))
            .min_by_key(|(_, entry)| entry.seq)
    }

    fn current_request_id(&self) -> Option<String> {
        self.global_front()
            .map(|(_, entry)| entry.handle.request_id.clone())
    }

    fn current_request_id_for_run(&self, run_id: Option<&str>) -> Option<String> {
        self.by_owner
            .get(&owner_key(run_id))
            .and_then(|queue| queue.front())
            .map(|entry| entry.handle.request_id.clone())
    }

    fn current_actionable_request_id_for_run(&self, run_id: Option<&str>) -> Option<String> {
        let run_request_id = self.current_request_id_for_run(run_id)?;
        let global_request_id = self.current_request_id()?;
        (run_request_id == global_request_id).then_some(run_request_id)
    }

    fn locate_request(&self, request_id: &str) -> Option<(&str, usize)> {
        self.by_owner.iter().find_map(|(owner, queue)| {
            queue
                .iter()
                .position(|entry| entry.handle.request_id == request_id)
                .map(|index| (owner.as_str(), index))
        })
    }

    fn locate_request_owned(&self, request_id: &str) -> Option<(String, usize)> {
        self.locate_request(request_id)
            .map(|(owner, index)| (owner.to_string(), index))
    }

    fn remove_at(
        &mut self,
        owner: &str,
        index: usize,
        phase: InteractiveRequestPhase,
    ) -> Option<TerminalInteractiveRequest<T>> {
        let queue = self.by_owner.get_mut(owner)?;
        let entry = queue.remove(index)?;
        let should_cleanup = queue.is_empty();
        if should_cleanup {
            self.by_owner.remove(owner);
        }
        Some(TerminalInteractiveRequest {
            handle: entry.handle.into_terminal(phase),
            reply: entry.reply,
        })
    }

    fn pop_global_front(
        &mut self,
        phase: InteractiveRequestPhase,
    ) -> Option<TerminalInteractiveRequest<T>> {
        let owner = self.global_front().map(|(owner, _)| owner.to_string())?;
        self.remove_at(&owner, 0, phase)
    }
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
    pending: Arc<Mutex<PendingInteractiveQueues<T>>>,
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
            pending: Arc::new(Mutex::new(PendingInteractiveQueues::default())),
        }
    }

    pub const fn kind(&self) -> InteractiveRequestKind {
        self.kind
    }

    pub const fn timeout(&self) -> Duration {
        self.timeout_policy.timeout_for(self.kind)
    }

    pub async fn register(&self, reply: oneshot::Sender<T>) -> InteractiveRequestHandle {
        self.register_with_run_id(reply, None).await
    }

    pub async fn register_with_run_id(
        &self,
        reply: oneshot::Sender<T>,
        run_id: Option<String>,
    ) -> InteractiveRequestHandle {
        let handle = InteractiveRequestHandle::pending(self.kind, run_id);
        let mut pending = self.pending.lock().await;
        let seq = pending.next_seq;
        pending.next_seq = pending.next_seq.wrapping_add(1);
        pending
            .by_owner
            .entry(owner_key(handle.run_id.as_deref()))
            .or_default()
            .push_back(PendingInteractiveRequest {
                seq,
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
        let entry = match request_id {
            Some(expected_request_id) => {
                let located = pending.locate_request_owned(expected_request_id);
                match located {
                    Some((owner, 0)) => pending
                        .remove_at(&owner, 0, InteractiveRequestPhase::Resolved)
                        .expect("front pending request should exist"),
                    Some((owner, _)) => {
                        let current_request_id = pending
                            .current_request_id_for_run(owner_run_id(&owner))
                            .expect("owner queue should have a front request");
                        return Err(ResolveInteractiveRequestError::StaleRequestId {
                            kind: self.kind,
                            request_id: expected_request_id.to_string(),
                            current_request_id,
                        });
                    }
                    None => match pending.current_request_id() {
                        Some(current_request_id) => {
                            return Err(ResolveInteractiveRequestError::StaleRequestId {
                                kind: self.kind,
                                request_id: expected_request_id.to_string(),
                                current_request_id,
                            });
                        }
                        None => {
                            return Err(ResolveInteractiveRequestError::MissingPendingRequest {
                                kind: self.kind,
                            });
                        }
                    },
                }
            }
            None => match pending.pop_global_front(InteractiveRequestPhase::Resolved) {
                Some(entry) => entry,
                None => {
                    return Err(ResolveInteractiveRequestError::MissingPendingRequest {
                        kind: self.kind,
                    });
                }
            },
        };

        Ok(entry)
    }

    pub async fn timeout_request(&self, request_id: &str) -> Option<TerminalInteractiveRequest<T>> {
        let mut pending = self.pending.lock().await;
        let located = pending.locate_request_owned(request_id);
        match located {
            Some((owner, 0)) => pending.remove_at(&owner, 0, InteractiveRequestPhase::TimedOut),
            _ => None,
        }
    }

    pub async fn await_timeout_request(
        &self,
        request_id: &str,
    ) -> Option<TerminalInteractiveRequest<T>> {
        let timeout = self.timeout();
        let poll_interval = timeout_watchdog_poll_interval(timeout);

        loop {
            let state = {
                let pending = self.pending.lock().await;
                match pending.locate_request(request_id) {
                    Some((_, 0)) => PendingTimeoutState::Current,
                    Some((_, _)) => PendingTimeoutState::Queued,
                    None => PendingTimeoutState::Missing,
                }
            };

            match state {
                PendingTimeoutState::Current => {
                    tokio::time::sleep(timeout).await;
                    return self.timeout_request(request_id).await;
                }
                PendingTimeoutState::Queued => tokio::time::sleep(poll_interval).await,
                PendingTimeoutState::Missing => return None,
            }
        }
    }

    pub async fn cancel_pending(&self) -> Option<TerminalInteractiveRequest<T>> {
        self.pending
            .lock()
            .await
            .pop_global_front(InteractiveRequestPhase::Cancelled)
    }

    pub async fn cancel_pending_for_run(
        &self,
        run_id: &str,
    ) -> Option<TerminalInteractiveRequest<T>> {
        let owner = owner_key(Some(run_id));
        self.pending
            .lock()
            .await
            .remove_at(&owner, 0, InteractiveRequestPhase::Cancelled)
    }

    pub async fn current_request_id(&self) -> Option<String> {
        self.pending.lock().await.current_request_id()
    }

    pub async fn current_request_id_for_run(&self, run_id: Option<&str>) -> Option<String> {
        self.pending.lock().await.current_request_id_for_run(run_id)
    }

    pub async fn current_actionable_request_id_for_run(
        &self,
        run_id: Option<&str>,
    ) -> Option<String> {
        self.pending
            .lock()
            .await
            .current_actionable_request_id_for_run(run_id)
    }
}

#[derive(Clone, Copy)]
enum PendingTimeoutState {
    Current,
    Queued,
    Missing,
}

fn timeout_watchdog_poll_interval(timeout: Duration) -> Duration {
    let candidate = timeout.checked_div(10).unwrap_or(timeout);
    candidate.clamp(
        MIN_TIMEOUT_WATCHDOG_POLL_INTERVAL,
        MAX_TIMEOUT_WATCHDOG_POLL_INTERVAL,
    )
}

const GLOBAL_INTERACTIVE_OWNER: &str = "__interactive_global__";

fn owner_key(run_id: Option<&str>) -> String {
    run_id.unwrap_or(GLOBAL_INTERACTIVE_OWNER).to_string()
}

fn owner_run_id(owner: &str) -> Option<&str> {
    (owner != GLOBAL_INTERACTIVE_OWNER).then_some(owner)
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::sync::oneshot;

    fn short_timeout_policy() -> InteractiveTimeoutPolicy {
        InteractiveTimeoutPolicy::new(
            Duration::from_millis(25),
            Duration::from_millis(25),
            Duration::from_millis(25),
        )
    }

    async fn assert_hidden_queued_request_does_not_timeout_before_promotion(
        kind: InteractiveRequestKind,
    ) {
        let store =
            InteractiveRequestStore::<String>::with_timeout_policy(kind, short_timeout_policy());
        let (first_tx, _first_rx) = oneshot::channel();
        let first = store.register(first_tx).await;
        let (second_tx, _second_rx) = oneshot::channel();
        let second = store.register(second_tx).await;

        tokio::time::sleep(store.timeout()).await;

        assert!(
            store.timeout_request(&second.request_id).await.is_none(),
            "hidden queued request should stay pending until promotion"
        );
        assert_eq!(
            store.current_request_id().await,
            Some(first.request_id.clone())
        );

        let timed_out_first = store
            .timeout_request(&first.request_id)
            .await
            .expect("front request should time out once threshold elapses");
        assert_eq!(
            timed_out_first.handle.phase,
            InteractiveRequestPhase::TimedOut
        );
        assert_eq!(timed_out_first.handle.request_id, first.request_id);
        assert_eq!(
            store.current_request_id().await,
            Some(second.request_id.clone())
        );

        let timed_out_second = store
            .timeout_request(&second.request_id)
            .await
            .expect("promoted request should time out when targeted after promotion");
        assert_eq!(
            timed_out_second.handle.phase,
            InteractiveRequestPhase::TimedOut
        );
        assert_eq!(timed_out_second.handle.request_id, second.request_id);
        assert!(store.current_request_id().await.is_none());
    }

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
    async fn resolve_requires_matching_front_request_id_when_multiple_requests_are_queued() {
        let store = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Approval);
        let (first_tx, _first_rx) = oneshot::channel();
        let first = store.register(first_tx).await;
        let (second_tx, _second_rx) = oneshot::channel();
        let second = store.register(second_tx).await;

        let error = store
            .resolve(Some(&second.request_id))
            .await
            .expect_err("hidden queued request should not resolve out of order");

        assert_eq!(
            error,
            ResolveInteractiveRequestError::StaleRequestId {
                kind: InteractiveRequestKind::Approval,
                request_id: second.request_id.clone(),
                current_request_id: first.request_id.clone(),
            }
        );
        assert_eq!(
            store.current_request_id().await,
            Some(first.request_id.clone())
        );

        let resolved_first = store
            .resolve(Some(&first.request_id))
            .await
            .expect("front request should resolve first");
        assert_eq!(resolved_first.handle.request_id, first.request_id);
        assert_eq!(
            store.current_request_id().await,
            Some(second.request_id.clone())
        );

        let resolved_second = store
            .resolve(Some(&second.request_id))
            .await
            .expect("second request should resolve after promotion");
        assert_eq!(resolved_second.handle.request_id, second.request_id);
        assert!(store.current_request_id().await.is_none());
    }

    #[tokio::test]
    async fn different_runs_can_resolve_same_kind_requests_independently() {
        let store = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Question);
        let (run_a_tx, _run_a_rx) = oneshot::channel();
        let run_a = store
            .register_with_run_id(run_a_tx, Some("web-run-a".to_string()))
            .await;
        let (run_b_tx, _run_b_rx) = oneshot::channel();
        let run_b = store
            .register_with_run_id(run_b_tx, Some("web-run-b".to_string()))
            .await;

        assert_eq!(
            store.current_request_id_for_run(Some("web-run-a")).await,
            Some(run_a.request_id.clone())
        );
        assert_eq!(
            store.current_request_id_for_run(Some("web-run-b")).await,
            Some(run_b.request_id.clone())
        );

        let resolved_run_b = store
            .resolve(Some(&run_b.request_id))
            .await
            .expect("other run's front request should resolve without waiting for run A");
        assert_eq!(resolved_run_b.handle.request_id, run_b.request_id);
        assert_eq!(
            store.current_request_id_for_run(Some("web-run-a")).await,
            Some(run_a.request_id.clone())
        );

        let resolved_run_a = store
            .resolve(Some(&run_a.request_id))
            .await
            .expect("run A request should still resolve afterwards");
        assert_eq!(resolved_run_a.handle.request_id, run_a.request_id);
        assert!(store.current_request_id().await.is_none());
    }

    #[tokio::test]
    async fn actionable_request_for_run_requires_global_front_ownership() {
        let store = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Question);
        let (run_a_tx, _run_a_rx) = oneshot::channel();
        let run_a = store
            .register_with_run_id(run_a_tx, Some("web-run-a".to_string()))
            .await;
        let (run_b_tx, _run_b_rx) = oneshot::channel();
        let run_b = store
            .register_with_run_id(run_b_tx, Some("web-run-b".to_string()))
            .await;

        assert_eq!(
            store
                .current_actionable_request_id_for_run(Some("web-run-a"))
                .await,
            Some(run_a.request_id.clone())
        );
        assert_eq!(
            store
                .current_actionable_request_id_for_run(Some("web-run-b"))
                .await,
            None
        );

        let _ = store
            .resolve(Some(&run_a.request_id))
            .await
            .expect("run A request should resolve first");

        assert_eq!(
            store
                .current_actionable_request_id_for_run(Some("web-run-b"))
                .await,
            Some(run_b.request_id)
        );
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
    async fn queued_hidden_approval_timeout_waits_until_request_is_promoted() {
        assert_hidden_queued_request_does_not_timeout_before_promotion(
            InteractiveRequestKind::Approval,
        )
        .await;
    }

    #[tokio::test]
    async fn queued_hidden_question_timeout_waits_until_request_is_promoted() {
        assert_hidden_queued_request_does_not_timeout_before_promotion(
            InteractiveRequestKind::Question,
        )
        .await;
    }

    #[tokio::test]
    async fn queued_hidden_plan_timeout_waits_until_request_is_promoted() {
        assert_hidden_queued_request_does_not_timeout_before_promotion(
            InteractiveRequestKind::Plan,
        )
        .await;
    }

    #[tokio::test]
    async fn watchdog_timeout_window_starts_after_hidden_request_is_promoted() {
        let store = InteractiveRequestStore::<String>::with_timeout_policy(
            InteractiveRequestKind::Question,
            short_timeout_policy(),
        );
        let (first_tx, _first_rx) = oneshot::channel();
        let first = store.register(first_tx).await;
        let (second_tx, _second_rx) = oneshot::channel();
        let second = store.register(second_tx).await;

        let timeout_task = {
            let store = store.clone();
            let request_id = second.request_id.clone();
            tokio::spawn(async move {
                store
                    .await_timeout_request(&request_id)
                    .await
                    .map(|reply| reply.handle.request_id)
            })
        };

        tokio::time::sleep(store.timeout() + Duration::from_millis(10)).await;
        assert!(
            !timeout_task.is_finished(),
            "hidden queued request watchdog should keep waiting for promotion"
        );

        let _ = store
            .resolve(Some(&first.request_id))
            .await
            .expect("front request should resolve before the hidden watchdog fires");

        tokio::time::sleep(Duration::from_millis(10)).await;
        assert!(
            !timeout_task.is_finished(),
            "promoted request should receive a fresh timeout window"
        );

        let timed_out_request_id = timeout_task
            .await
            .expect("watchdog task should finish")
            .expect("promoted request should eventually time out");
        assert_eq!(timed_out_request_id, second.request_id);
        assert!(store.current_request_id().await.is_none());
    }

    #[tokio::test]
    async fn watchdog_timeout_for_one_run_does_not_wait_for_other_runs() {
        let store = InteractiveRequestStore::<String>::with_timeout_policy(
            InteractiveRequestKind::Approval,
            short_timeout_policy(),
        );
        let (run_a_tx, _run_a_rx) = oneshot::channel();
        let run_a = store
            .register_with_run_id(run_a_tx, Some("web-run-a".to_string()))
            .await;
        let (run_b_tx, _run_b_rx) = oneshot::channel();
        let run_b = store
            .register_with_run_id(run_b_tx, Some("web-run-b".to_string()))
            .await;

        let timeout_task = {
            let store = store.clone();
            let request_id = run_b.request_id.clone();
            tokio::spawn(async move {
                store
                    .await_timeout_request(&request_id)
                    .await
                    .map(|reply| reply.handle.request_id)
            })
        };

        let timed_out_request_id = timeout_task
            .await
            .expect("watchdog task should finish")
            .expect("run B request should time out independently");
        assert_eq!(timed_out_request_id, run_b.request_id);
        assert_eq!(
            store.current_request_id_for_run(Some("web-run-a")).await,
            Some(run_a.request_id)
        );
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

    #[tokio::test]
    async fn run_correlation_survives_timeout_and_cancel_cleanup() {
        let timeout_store =
            InteractiveRequestStore::<String>::new(InteractiveRequestKind::Question);
        let (timeout_tx, _timeout_rx) = oneshot::channel();
        let timeout_handle = timeout_store
            .register_with_run_id(timeout_tx, Some("desktop-run-timeout".to_string()))
            .await;

        let timed_out = timeout_store
            .timeout_request(&timeout_handle.request_id)
            .await
            .expect("matching request should time out");
        assert_eq!(timed_out.handle.phase, InteractiveRequestPhase::TimedOut);
        assert_eq!(
            timed_out.handle.run_id.as_deref(),
            Some("desktop-run-timeout")
        );

        let cancel_store = InteractiveRequestStore::<String>::new(InteractiveRequestKind::Plan);
        let (cancel_tx, _cancel_rx) = oneshot::channel();
        cancel_store
            .register_with_run_id(cancel_tx, Some("desktop-run-cancel".to_string()))
            .await;

        let cancelled = cancel_store
            .cancel_pending()
            .await
            .expect("pending request should be cancelled");
        assert_eq!(cancelled.handle.phase, InteractiveRequestPhase::Cancelled);
        assert_eq!(
            cancelled.handle.run_id.as_deref(),
            Some("desktop-run-cancel")
        );
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
