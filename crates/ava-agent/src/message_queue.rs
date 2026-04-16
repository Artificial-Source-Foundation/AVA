//! Three-tier message queue for mid-stream user messaging.
//!
//! Receives [`QueuedMessage`] items from the TUI via a tokio mpsc channel and
//! routes them into the correct internal queue based on their [`MessageTier`]:
//!
//! - **Steering** (Tier 1): drained between tool calls, causes remaining tools to be skipped.
//! - **Follow-up** (Tier 2): drained after the agent loop finishes a task (no more tools/steering).
//! - **Post-complete** (Tier 3): grouped pipeline stages that run after the agent is fully done.

use std::collections::{BTreeMap, VecDeque};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use tokio::sync::mpsc;

use ava_types::{MessageTier, QueuedMessage};

/// A three-tier message queue that sits inside the agent's async task.
///
/// The TUI sends messages via an `mpsc::Sender<QueuedMessage>`.
/// The agent loop calls [`poll()`](MessageQueue::poll) to drain the channel
/// and sort messages into the correct tier.
pub struct MessageQueue {
    steering: VecDeque<String>,
    follow_up: VecDeque<String>,
    /// group_id -> ordered list of messages in that group.
    post_complete: BTreeMap<u32, Vec<String>>,
    /// The group that new post-complete messages (without an explicit group) are assigned to.
    current_post_group: u32,
    /// Whether a post-complete group is currently executing.
    group_running: bool,
    rx: mpsc::UnboundedReceiver<QueuedMessage>,
    clear_steering_requested: Arc<AtomicBool>,
}

#[derive(Clone, Default)]
pub struct MessageQueueControl {
    clear_steering_requested: Arc<AtomicBool>,
}

impl MessageQueueControl {
    pub fn clear_steering(&self) {
        self.clear_steering_requested.store(true, Ordering::SeqCst);
    }
}

impl MessageQueue {
    /// Create a new queue, returning the queue and the sender for the TUI.
    pub fn new() -> (Self, mpsc::UnboundedSender<QueuedMessage>) {
        let (queue, tx, _) = Self::new_with_control();
        (queue, tx)
    }

    pub fn new_with_control() -> (
        Self,
        mpsc::UnboundedSender<QueuedMessage>,
        MessageQueueControl,
    ) {
        let (tx, rx) = mpsc::unbounded_channel();
        let clear_steering_requested = Arc::new(AtomicBool::new(false));
        (
            Self {
                steering: VecDeque::new(),
                follow_up: VecDeque::new(),
                post_complete: BTreeMap::new(),
                current_post_group: 1,
                group_running: false,
                rx,
                clear_steering_requested: clear_steering_requested.clone(),
            },
            tx,
            MessageQueueControl {
                clear_steering_requested,
            },
        )
    }

    /// Drain the channel and route each message to the correct internal queue.
    /// Call this frequently (e.g., between tool executions).
    pub fn poll(&mut self) {
        if self.clear_steering_requested.swap(false, Ordering::SeqCst) {
            self.steering.clear();
        }

        while let Ok(msg) = self.rx.try_recv() {
            if self.clear_steering_requested.swap(false, Ordering::SeqCst) {
                self.steering.clear();
            }

            match msg.tier {
                MessageTier::Steering => {
                    if !self.clear_steering_requested.load(Ordering::SeqCst) {
                        self.steering.push_back(msg.text);
                    }
                }
                MessageTier::FollowUp => {
                    self.follow_up.push_back(msg.text);
                }
                MessageTier::PostComplete { group } => {
                    self.post_complete.entry(group).or_default().push(msg.text);
                }
            }
        }
    }

    /// Take all steering messages (Tier 1).
    pub fn drain_steering(&mut self) -> Vec<String> {
        self.steering.drain(..).collect()
    }

    /// Take all follow-up messages (Tier 2).
    pub fn drain_follow_up(&mut self) -> Vec<String> {
        self.follow_up.drain(..).collect()
    }

    /// Pop the lowest-numbered post-complete group.
    /// Returns `Some((group_id, messages))` or `None` if no groups remain.
    pub fn next_post_complete_group(&mut self) -> Option<(u32, Vec<String>)> {
        let key = *self.post_complete.keys().next()?;
        let messages = self.post_complete.remove(&key)?;
        self.group_running = true;
        Some((key, messages))
    }

    /// Mark the current post-complete group as finished.
    pub fn finish_post_complete_group(&mut self) {
        self.group_running = false;
    }

    /// Returns `true` if there are any steering messages pending.
    pub fn has_steering(&self) -> bool {
        !self.steering.is_empty()
    }

    /// Returns `true` if there are any follow-up messages pending.
    pub fn has_follow_up(&self) -> bool {
        !self.follow_up.is_empty()
    }

    /// Returns `true` if there are any post-complete groups pending.
    pub fn has_post_complete(&self) -> bool {
        !self.post_complete.is_empty()
    }

    /// Returns the number of pending messages per tier: (steering, follow_up, post_complete).
    pub fn pending_count(&self) -> (usize, usize, usize) {
        let pc: usize = self.post_complete.values().map(|v| v.len()).sum();
        (self.steering.len(), self.follow_up.len(), pc)
    }

    /// The current post-complete group number. Used by the TUI to assign
    /// messages to the right group when no explicit group is specified.
    pub fn current_post_group(&self) -> u32 {
        if self.group_running {
            // If a group is currently running, new messages go to the next group
            self.current_post_group + 1
        } else {
            self.current_post_group
        }
    }

    /// Advance the current post-complete group counter.
    /// Called after a post-complete group finishes successfully.
    pub fn advance_post_group(&mut self) {
        self.current_post_group += 1;
    }

    /// Clear the steering queue (called on hard abort / Ctrl+C).
    pub fn clear_steering(&mut self) {
        self.clear_steering_requested.store(false, Ordering::SeqCst);
        self.steering.clear();
    }
}

// Ensure MessageQueue is Send (required for async task)
const _: () = {
    fn _assert_send<T: Send>() {}
    fn _check() {
        _assert_send::<MessageQueue>();
    }
};

#[cfg(test)]
mod tests {
    use super::*;

    fn send_msg(tx: &mpsc::UnboundedSender<QueuedMessage>, text: &str, tier: MessageTier) {
        tx.send(QueuedMessage {
            text: text.to_string(),
            tier,
        })
        .unwrap();
    }

    #[test]
    fn test_new_creates_empty_queue() {
        let (queue, _tx) = MessageQueue::new();
        assert!(!queue.has_steering());
        assert!(!queue.has_follow_up());
        assert!(!queue.has_post_complete());
        assert_eq!(queue.pending_count(), (0, 0, 0));
    }

    #[test]
    fn test_poll_routes_steering() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "steer me", MessageTier::Steering);
        send_msg(&tx, "steer again", MessageTier::Steering);

        queue.poll();
        assert!(queue.has_steering());
        assert!(!queue.has_follow_up());
        assert_eq!(queue.pending_count(), (2, 0, 0));

        let msgs = queue.drain_steering();
        assert_eq!(msgs, vec!["steer me", "steer again"]);
        assert!(!queue.has_steering());
    }

    #[test]
    fn test_poll_routes_follow_up() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "follow up 1", MessageTier::FollowUp);
        send_msg(&tx, "follow up 2", MessageTier::FollowUp);

        queue.poll();
        assert!(queue.has_follow_up());
        assert_eq!(queue.pending_count(), (0, 2, 0));

        let msgs = queue.drain_follow_up();
        assert_eq!(msgs, vec!["follow up 1", "follow up 2"]);
        assert!(!queue.has_follow_up());
    }

    #[test]
    fn test_poll_routes_post_complete() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "pc 1a", MessageTier::PostComplete { group: 1 });
        send_msg(&tx, "pc 1b", MessageTier::PostComplete { group: 1 });
        send_msg(&tx, "pc 2a", MessageTier::PostComplete { group: 2 });

        queue.poll();
        assert!(queue.has_post_complete());
        assert_eq!(queue.pending_count(), (0, 0, 3));

        let (gid, msgs) = queue.next_post_complete_group().unwrap();
        assert_eq!(gid, 1);
        assert_eq!(msgs, vec!["pc 1a", "pc 1b"]);

        let (gid2, msgs2) = queue.next_post_complete_group().unwrap();
        assert_eq!(gid2, 2);
        assert_eq!(msgs2, vec!["pc 2a"]);

        assert!(!queue.has_post_complete());
        assert!(queue.next_post_complete_group().is_none());
    }

    #[test]
    fn test_mixed_tiers() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "steering", MessageTier::Steering);
        send_msg(&tx, "follow", MessageTier::FollowUp);
        send_msg(&tx, "later", MessageTier::PostComplete { group: 1 });

        queue.poll();
        assert_eq!(queue.pending_count(), (1, 1, 1));
        assert!(queue.has_steering());
        assert!(queue.has_follow_up());
        assert!(queue.has_post_complete());
    }

    #[test]
    fn test_drain_is_idempotent() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "steer", MessageTier::Steering);
        queue.poll();

        let first = queue.drain_steering();
        assert_eq!(first.len(), 1);
        let second = queue.drain_steering();
        assert!(second.is_empty());
    }

    #[test]
    fn test_clear_steering() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "s1", MessageTier::Steering);
        send_msg(&tx, "f1", MessageTier::FollowUp);
        queue.poll();

        queue.clear_steering();
        assert!(!queue.has_steering());
        assert!(queue.has_follow_up()); // follow-up preserved
    }

    #[test]
    fn test_current_post_group_advances() {
        let (mut queue, _tx) = MessageQueue::new();
        assert_eq!(queue.current_post_group(), 1);

        queue.advance_post_group();
        assert_eq!(queue.current_post_group(), 2);
    }

    #[test]
    fn test_current_post_group_during_execution() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "g1", MessageTier::PostComplete { group: 1 });
        queue.poll();

        // Start running group 1
        let _ = queue.next_post_complete_group();
        // While group 1 is running, new messages should target group 2
        assert_eq!(queue.current_post_group(), 2);

        queue.finish_post_complete_group();
        // After finishing, back to current
        assert_eq!(queue.current_post_group(), 1);
    }

    #[test]
    fn test_poll_multiple_times() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "s1", MessageTier::Steering);
        queue.poll();
        assert_eq!(queue.pending_count(), (1, 0, 0));

        send_msg(&tx, "s2", MessageTier::Steering);
        queue.poll();
        assert_eq!(queue.pending_count(), (2, 0, 0));
    }

    #[test]
    fn test_post_complete_groups_ordered() {
        let (mut queue, tx) = MessageQueue::new();
        // Insert in reverse order — BTreeMap should still yield 1, 2, 3
        send_msg(&tx, "g3", MessageTier::PostComplete { group: 3 });
        send_msg(&tx, "g1", MessageTier::PostComplete { group: 1 });
        send_msg(&tx, "g2", MessageTier::PostComplete { group: 2 });
        queue.poll();

        let (g1, _) = queue.next_post_complete_group().unwrap();
        assert_eq!(g1, 1);
        queue.finish_post_complete_group();
        let (g2, _) = queue.next_post_complete_group().unwrap();
        assert_eq!(g2, 2);
        queue.finish_post_complete_group();
        let (g3, _) = queue.next_post_complete_group().unwrap();
        assert_eq!(g3, 3);
    }

    #[test]
    fn test_empty_poll_is_noop() {
        let (mut queue, _tx) = MessageQueue::new();
        queue.poll(); // no messages sent
        assert_eq!(queue.pending_count(), (0, 0, 0));
    }

    #[test]
    fn test_poll_after_sender_dropped() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "before drop", MessageTier::Steering);
        drop(tx);

        // Should still drain any messages sent before the drop
        queue.poll();
        assert_eq!(queue.pending_count(), (1, 0, 0));

        // Subsequent polls should be no-ops (channel closed)
        queue.poll();
        assert_eq!(queue.pending_count(), (1, 0, 0));
    }

    #[test]
    fn test_drain_follow_up_then_steering_independent() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "steer", MessageTier::Steering);
        send_msg(&tx, "follow", MessageTier::FollowUp);
        queue.poll();

        // Draining one tier does not affect the other
        let steering = queue.drain_steering();
        assert_eq!(steering.len(), 1);
        assert!(queue.has_follow_up());

        let follow = queue.drain_follow_up();
        assert_eq!(follow.len(), 1);
    }

    #[test]
    fn test_clear_steering_preserves_post_complete() {
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "steer", MessageTier::Steering);
        send_msg(&tx, "pc", MessageTier::PostComplete { group: 1 });
        queue.poll();

        queue.clear_steering();
        assert!(!queue.has_steering());
        assert!(queue.has_post_complete());
        assert_eq!(queue.pending_count(), (0, 0, 1));
    }

    #[test]
    fn test_post_complete_group_zero() {
        // Group 0 should work and come before group 1
        let (mut queue, tx) = MessageQueue::new();
        send_msg(&tx, "g1", MessageTier::PostComplete { group: 1 });
        send_msg(&tx, "g0", MessageTier::PostComplete { group: 0 });
        queue.poll();

        let (gid, _) = queue.next_post_complete_group().unwrap();
        assert_eq!(gid, 0);
    }
}
