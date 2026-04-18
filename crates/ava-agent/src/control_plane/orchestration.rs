//! Shared deferred-queue orchestration helpers.
//!
//! Ownership: parsing queue progress markers and synchronizing deferred vs in-flight
//! follow-up/post-complete queue state across adapters.

use std::collections::{HashMap, VecDeque};

use tokio::sync::RwLock;
use uuid::Uuid;

use ava_types::{MessageTier, QueuedMessage};

pub fn queued_post_complete_group(progress: &str) -> Option<u32> {
    progress
        .strip_prefix("post-complete group ")?
        .split(':')
        .next()?
        .parse()
        .ok()
}

pub fn is_inactive_scoped_status_lookup(
    requested_run_id: Option<&str>,
    requested_session_id: Option<Uuid>,
    message: &str,
) -> bool {
    match (requested_run_id, requested_session_id) {
        (Some(run_id), Some(session_id)) => {
            message == format!("Run {run_id} is not active")
                || message == format!("Session {session_id} does not have an active run")
        }
        (Some(run_id), None) => message == format!("Run {run_id} is not active"),
        (None, Some(session_id)) => {
            message == format!("Session {session_id} does not have an active run")
        }
        (None, None) => false,
    }
}

pub async fn sync_deferred_queues_for_progress(
    progress: &str,
    session_id: Uuid,
    deferred: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
    in_flight: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
) -> bool {
    let update = if let Some(text) = progress.strip_prefix("follow-up: ") {
        DeferredQueueProgressUpdate::FollowUp(text)
    } else if let Some(group_id) = queued_post_complete_group(progress) {
        DeferredQueueProgressUpdate::PostCompleteGroup(group_id)
    } else {
        return false;
    };

    with_session_queues_mut(
        session_id,
        deferred,
        in_flight,
        |session_deferred, session_in_flight| match update {
            DeferredQueueProgressUpdate::FollowUp(text) => {
                move_follow_up_to_in_flight(session_deferred, session_in_flight, text);
            }
            DeferredQueueProgressUpdate::PostCompleteGroup(group_id) => {
                move_post_complete_group_to_in_flight(
                    session_deferred,
                    session_in_flight,
                    group_id,
                );
            }
        },
    )
    .await;

    true
}

pub async fn restore_in_flight_deferred(
    session_id: Uuid,
    deferred: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
    in_flight: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
) {
    with_queue_maps_mut(deferred, in_flight, |deferred_map, in_flight_map| {
        let Some(mut session_in_flight) = in_flight_map.remove(&session_id) else {
            return;
        };

        if session_in_flight.is_empty() {
            return;
        }

        let session_deferred = deferred_map.entry(session_id).or_default();
        while let Some(message) = session_in_flight.pop_back() {
            session_deferred.push_front(message);
        }
    })
    .await;
}

pub async fn clear_preserved_deferred(
    session_id: Uuid,
    deferred: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
    in_flight: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
) {
    with_queue_maps_mut(deferred, in_flight, |deferred_map, in_flight_map| {
        deferred_map.remove(&session_id);
        in_flight_map.remove(&session_id);
    })
    .await;
}

enum DeferredQueueProgressUpdate<'a> {
    FollowUp(&'a str),
    PostCompleteGroup(u32),
}

async fn with_queue_maps_mut<R>(
    deferred: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
    in_flight: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
    mutate: impl FnOnce(
        &mut HashMap<Uuid, VecDeque<QueuedMessage>>,
        &mut HashMap<Uuid, VecDeque<QueuedMessage>>,
    ) -> R,
) -> R {
    let mut deferred_guard = deferred.write().await;
    let mut in_flight_guard = in_flight.write().await;
    mutate(&mut deferred_guard, &mut in_flight_guard)
}

async fn with_session_queues_mut<R>(
    session_id: Uuid,
    deferred: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
    in_flight: &RwLock<HashMap<Uuid, VecDeque<QueuedMessage>>>,
    mutate: impl FnOnce(&mut VecDeque<QueuedMessage>, &mut VecDeque<QueuedMessage>) -> R,
) -> R {
    with_queue_maps_mut(deferred, in_flight, |deferred_map, in_flight_map| {
        let session_deferred = deferred_map.entry(session_id).or_default();
        let session_in_flight = in_flight_map.entry(session_id).or_default();
        mutate(session_deferred, session_in_flight)
    })
    .await
}

fn move_follow_up_to_in_flight(
    deferred: &mut VecDeque<QueuedMessage>,
    in_flight: &mut VecDeque<QueuedMessage>,
    text: &str,
) {
    in_flight.retain(|message| !matches!(message.tier, MessageTier::FollowUp));
    if let Some(index) = deferred
        .iter()
        .position(|queued| queued.text == text && matches!(queued.tier, MessageTier::FollowUp))
    {
        if let Some(message) = deferred.remove(index) {
            in_flight.push_back(message);
        }
    }
}

fn move_post_complete_group_to_in_flight(
    deferred: &mut VecDeque<QueuedMessage>,
    in_flight: &mut VecDeque<QueuedMessage>,
    group_id: u32,
) {
    in_flight.retain(|message| !matches!(message.tier, MessageTier::PostComplete { .. }));
    let mut retained = VecDeque::new();
    while let Some(message) = deferred.pop_front() {
        if matches!(message.tier, MessageTier::PostComplete { group } if group == group_id) {
            in_flight.push_back(message);
        } else {
            retained.push_back(message);
        }
    }
    *deferred = retained;
}

#[cfg(test)]
mod tests {
    use super::*;

    fn queued_message(text: &str, tier: MessageTier) -> QueuedMessage {
        QueuedMessage {
            text: text.to_string(),
            tier,
        }
    }

    #[test]
    fn queued_post_complete_group_parses_group_prefix() {
        assert_eq!(
            queued_post_complete_group("post-complete group 7: started"),
            Some(7)
        );
        assert_eq!(queued_post_complete_group("post-complete group nope"), None);
        assert_eq!(queued_post_complete_group("follow-up: hello"), None);
    }

    #[test]
    fn inactive_scoped_status_lookup_matches_expected_inactive_messages() {
        let session_id = Uuid::new_v4();

        assert!(is_inactive_scoped_status_lookup(
            Some("desktop-run-a"),
            Some(session_id),
            &format!("Run {} is not active", "desktop-run-a")
        ));
        assert!(is_inactive_scoped_status_lookup(
            None,
            Some(session_id),
            &format!("Session {session_id} does not have an active run")
        ));
        assert!(!is_inactive_scoped_status_lookup(
            Some("desktop-run-a"),
            Some(session_id),
            &format!("Run {} does not own session {session_id}", "desktop-run-a")
        ));
    }

    #[tokio::test]
    async fn sync_deferred_queues_for_progress_promotes_follow_up_messages() {
        let session_id = Uuid::new_v4();
        let deferred = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([
                queued_message("keep", MessageTier::Steering),
                queued_message("target", MessageTier::FollowUp),
            ]),
        )]));
        let in_flight = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([queued_message("stale", MessageTier::FollowUp)]),
        )]));

        assert!(
            sync_deferred_queues_for_progress(
                "follow-up: target",
                session_id,
                &deferred,
                &in_flight,
            )
            .await
        );

        let deferred_guard = deferred.read().await;
        let session_deferred = deferred_guard.get(&session_id).expect("deferred queue");
        assert_eq!(session_deferred.len(), 1);
        assert_eq!(session_deferred[0].text, "keep");

        let in_flight_guard = in_flight.read().await;
        let session_in_flight = in_flight_guard.get(&session_id).expect("in-flight queue");
        assert_eq!(session_in_flight.len(), 1);
        assert_eq!(session_in_flight[0].text, "target");
        assert!(matches!(session_in_flight[0].tier, MessageTier::FollowUp));
    }

    #[tokio::test]
    async fn sync_deferred_queues_for_progress_promotes_post_complete_group() {
        let session_id = Uuid::new_v4();
        let deferred = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([
                queued_message("group-1-a", MessageTier::PostComplete { group: 1 }),
                queued_message("group-2", MessageTier::PostComplete { group: 2 }),
                queued_message("group-1-b", MessageTier::PostComplete { group: 1 }),
            ]),
        )]));
        let in_flight = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([queued_message(
                "stale-group",
                MessageTier::PostComplete { group: 99 },
            )]),
        )]));

        assert!(
            sync_deferred_queues_for_progress(
                "post-complete group 1: started",
                session_id,
                &deferred,
                &in_flight,
            )
            .await
        );

        let deferred_guard = deferred.read().await;
        let session_deferred = deferred_guard.get(&session_id).expect("deferred queue");
        assert_eq!(session_deferred.len(), 1);
        assert_eq!(session_deferred[0].text, "group-2");

        let in_flight_guard = in_flight.read().await;
        let session_in_flight = in_flight_guard.get(&session_id).expect("in-flight queue");
        assert_eq!(session_in_flight.len(), 2);
        assert_eq!(session_in_flight[0].text, "group-1-a");
        assert_eq!(session_in_flight[1].text, "group-1-b");
    }

    #[tokio::test]
    async fn sync_deferred_queues_for_progress_ignores_non_queue_progress() {
        let session_id = Uuid::new_v4();
        let deferred = RwLock::new(HashMap::new());
        let in_flight = RwLock::new(HashMap::new());

        assert!(
            !sync_deferred_queues_for_progress("tool: edit", session_id, &deferred, &in_flight,)
                .await
        );
    }

    #[tokio::test]
    async fn restore_in_flight_deferred_requeues_messages_in_original_order() {
        let session_id = Uuid::new_v4();
        let deferred = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([queued_message("existing", MessageTier::Steering)]),
        )]));
        let in_flight = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([
                queued_message("first", MessageTier::FollowUp),
                queued_message("second", MessageTier::PostComplete { group: 2 }),
            ]),
        )]));

        restore_in_flight_deferred(session_id, &deferred, &in_flight).await;

        let deferred_guard = deferred.read().await;
        let session_deferred = deferred_guard.get(&session_id).expect("deferred queue");
        assert_eq!(session_deferred.len(), 3);
        assert_eq!(session_deferred[0].text, "first");
        assert_eq!(session_deferred[1].text, "second");
        assert_eq!(session_deferred[2].text, "existing");

        assert!(in_flight.read().await.get(&session_id).is_none());
    }

    #[tokio::test]
    async fn clear_preserved_deferred_removes_both_queue_views() {
        let session_id = Uuid::new_v4();
        let deferred = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([queued_message("deferred", MessageTier::FollowUp)]),
        )]));
        let in_flight = RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([queued_message(
                "in-flight",
                MessageTier::PostComplete { group: 1 },
            )]),
        )]));

        clear_preserved_deferred(session_id, &deferred, &in_flight).await;

        assert!(deferred.read().await.get(&session_id).is_none());
        assert!(in_flight.read().await.get(&session_id).is_none());
    }

    #[tokio::test]
    async fn concurrent_queue_helpers_complete_without_lock_inversion() {
        use std::time::Duration;

        let session_id = Uuid::new_v4();
        let deferred = std::sync::Arc::new(RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([queued_message("follow-up", MessageTier::FollowUp)]),
        )])));
        let in_flight = std::sync::Arc::new(RwLock::new(HashMap::from([(
            session_id,
            VecDeque::from([queued_message(
                "group-1",
                MessageTier::PostComplete { group: 1 },
            )]),
        )])));

        tokio::time::timeout(Duration::from_millis(250), async {
            for _ in 0..16 {
                let deferred_for_sync = deferred.clone();
                let in_flight_for_sync = in_flight.clone();
                let deferred_for_restore = deferred.clone();
                let in_flight_for_restore = in_flight.clone();
                let deferred_for_clear = deferred.clone();
                let in_flight_for_clear = in_flight.clone();

                let sync = tokio::spawn(async move {
                    sync_deferred_queues_for_progress(
                        "follow-up: follow-up",
                        session_id,
                        &deferred_for_sync,
                        &in_flight_for_sync,
                    )
                    .await
                });
                let restore = tokio::spawn(async move {
                    restore_in_flight_deferred(
                        session_id,
                        &deferred_for_restore,
                        &in_flight_for_restore,
                    )
                    .await;
                });
                let clear = tokio::spawn(async move {
                    clear_preserved_deferred(session_id, &deferred_for_clear, &in_flight_for_clear)
                        .await;
                });

                let _ = sync.await.expect("sync task should finish");
                restore.await.expect("restore task should finish");
                clear.await.expect("clear task should finish");
            }
        })
        .await
        .expect("queue helper operations should complete without deadlocking");
    }
}
