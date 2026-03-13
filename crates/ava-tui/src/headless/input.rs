use crate::config::cli::CliArgs;
use ava_agent::message_queue::MessageQueue;
use ava_types::{MessageTier, QueuedMessage};
use tokio::io::AsyncBufReadExt;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use tracing::debug;

pub(super) fn populate_queue_from_cli(cli: &CliArgs, tx: &mpsc::UnboundedSender<QueuedMessage>) {
    for msg in &cli.follow_up {
        debug!(text = %msg, "Pre-queuing follow-up message from CLI");
        let _ = tx.send(QueuedMessage {
            text: msg.clone(),
            tier: MessageTier::FollowUp,
        });
    }

    for (i, msg) in cli.later.iter().enumerate() {
        let group = (i + 1) as u32;
        debug!(text = %msg, group, "Pre-queuing post-complete message from CLI");
        let _ = tx.send(QueuedMessage {
            text: msg.clone(),
            tier: MessageTier::PostComplete { group },
        });
    }

    let pairs = cli.later_group.chunks(2);
    for chunk in pairs {
        if chunk.len() == 2 {
            if let Ok(group) = chunk[0].parse::<u32>() {
                debug!(text = %chunk[1], group, "Pre-queuing post-complete message (explicit group) from CLI");
                let _ = tx.send(QueuedMessage {
                    text: chunk[1].clone(),
                    tier: MessageTier::PostComplete { group },
                });
            } else {
                eprintln!(
                    "[warning] --later-group: invalid group number '{}', skipping",
                    chunk[0]
                );
            }
        }
    }
}

pub(super) fn parse_stdin_message(line: &str) -> Option<QueuedMessage> {
    let line = line.trim();
    if line.is_empty() {
        return None;
    }

    if let Some(rest) = line.strip_prefix(">>") {
        let rest = rest.trim_start();
        let mut chars = rest.chars().peekable();
        let mut digits = String::new();
        while let Some(&c) = chars.peek() {
            if c.is_ascii_digit() {
                digits.push(c);
                chars.next();
            } else {
                break;
            }
        }
        if !digits.is_empty() {
            let group: u32 = digits.parse().unwrap_or(1);
            let text: String = chars.collect();
            let text = text.trim().to_string();
            if text.is_empty() {
                return None;
            }
            Some(QueuedMessage {
                text,
                tier: MessageTier::PostComplete { group },
            })
        } else {
            let text = rest.to_string();
            if text.is_empty() {
                return None;
            }
            Some(QueuedMessage {
                text,
                tier: MessageTier::PostComplete { group: 1 },
            })
        }
    } else if let Some(rest) = line.strip_prefix('>') {
        let text = rest.trim().to_string();
        if text.is_empty() {
            return None;
        }
        Some(QueuedMessage {
            text,
            tier: MessageTier::FollowUp,
        })
    } else if let Some(rest) = line.strip_prefix('!') {
        let text = rest.trim().to_string();
        if text.is_empty() {
            return None;
        }
        Some(QueuedMessage {
            text,
            tier: MessageTier::Steering,
        })
    } else {
        Some(QueuedMessage {
            text: line.to_string(),
            tier: MessageTier::Steering,
        })
    }
}

pub(super) fn parse_json_stdin_message(line: &str) -> Option<QueuedMessage> {
    let v: serde_json::Value = serde_json::from_str(line).ok()?;
    let text = v.get("text")?.as_str()?.to_string();
    if text.is_empty() {
        return None;
    }
    let tier_str = v.get("tier").and_then(|t| t.as_str()).unwrap_or("steering");
    let tier = match tier_str {
        "steering" => MessageTier::Steering,
        "follow-up" | "followup" | "follow_up" => MessageTier::FollowUp,
        "post-complete" | "postcomplete" | "post_complete" => {
            let group = v.get("group").and_then(|g| g.as_u64()).unwrap_or(1) as u32;
            MessageTier::PostComplete { group }
        }
        _ => MessageTier::Steering,
    };
    Some(QueuedMessage { text, tier })
}

pub(super) fn spawn_stdin_reader(
    tx: mpsc::UnboundedSender<QueuedMessage>,
    json_mode: bool,
    cancel: CancellationToken,
) {
    tokio::spawn(async move {
        let stdin = tokio::io::stdin();
        let reader = tokio::io::BufReader::new(stdin);
        let mut lines = reader.lines();

        loop {
            tokio::select! {
                _ = cancel.cancelled() => break,
                result = lines.next_line() => {
                    match result {
                        Ok(Some(line)) => {
                            let msg = if json_mode {
                                parse_json_stdin_message(&line)
                            } else {
                                parse_stdin_message(&line)
                            };
                            if let Some(msg) = msg {
                                debug!(tier = ?msg.tier, text = %msg.text, "Received stdin message");
                                if tx.send(msg).is_err() {
                                    break;
                                }
                            }
                        }
                        Ok(None) => break,
                        Err(_) => break,
                    }
                }
            }
        }
    });
}

#[allow(dead_code)]
fn _queue_type_anchor(_: &MessageQueue) {}
