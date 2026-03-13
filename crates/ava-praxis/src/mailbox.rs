use std::collections::{HashMap, VecDeque};
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PeerMessageKind {
    TaskUpdate,
    DependencyNotice,
    Blocker,
    ReviewRequest,
    Custom(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PeerMessage {
    pub id: Uuid,
    pub from_worker: Uuid,
    pub to_worker: Uuid,
    pub kind: PeerMessageKind,
    pub body: String,
}

impl PeerMessage {
    pub fn new(
        from_worker: Uuid,
        to_worker: Uuid,
        kind: PeerMessageKind,
        body: impl Into<String>,
    ) -> Self {
        Self {
            id: Uuid::new_v4(),
            from_worker,
            to_worker,
            kind,
            body: body.into(),
        }
    }
}

#[derive(Debug, Default)]
pub struct Mailbox {
    inboxes: HashMap<Uuid, VecDeque<PeerMessage>>,
    audit_log: Vec<PeerMessage>,
}

impl Mailbox {
    pub fn send(&mut self, message: PeerMessage) {
        self.inboxes
            .entry(message.to_worker)
            .or_default()
            .push_back(message.clone());
        self.audit_log.push(message);
    }

    pub fn receive(&mut self, worker_id: Uuid) -> Option<PeerMessage> {
        self.inboxes
            .get_mut(&worker_id)
            .and_then(VecDeque::pop_front)
    }

    pub fn pending_count(&self, worker_id: Uuid) -> usize {
        self.inboxes.get(&worker_id).map_or(0, VecDeque::len)
    }

    pub fn audit_log(&self) -> &[PeerMessage] {
        &self.audit_log
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mailbox_send_receive_preserves_order() {
        let mut mailbox = Mailbox::default();
        let a = Uuid::new_v4();
        let b = Uuid::new_v4();

        mailbox.send(PeerMessage::new(a, b, PeerMessageKind::TaskUpdate, "first"));
        mailbox.send(PeerMessage::new(
            a,
            b,
            PeerMessageKind::TaskUpdate,
            "second",
        ));

        assert_eq!(mailbox.pending_count(b), 2);
        assert_eq!(mailbox.receive(b).expect("first msg").body, "first");
        assert_eq!(mailbox.receive(b).expect("second msg").body, "second");
        assert!(mailbox.receive(b).is_none());
        assert_eq!(mailbox.audit_log().len(), 2);
    }
}
