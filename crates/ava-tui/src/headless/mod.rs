use crate::config::cli::CliArgs;
use color_eyre::eyre::{eyre, Result};
use tracing::instrument;

mod input;
mod single;
mod watch;

#[cfg(feature = "voice")]
mod voice;

pub(crate) fn spawn_auto_approve_requests(
    mut approval_rx: tokio::sync::mpsc::UnboundedReceiver<
        ava_tools::permission_middleware::ApprovalRequest,
    >,
) {
    tokio::spawn(async move {
        while let Some(req) = approval_rx.recv().await {
            let _ = req
                .reply
                .send(ava_tools::permission_middleware::ToolApproval::Allowed);
        }
    });
}

#[instrument(skip(cli))]
pub async fn run_headless(cli: CliArgs) -> Result<()> {
    if cli.watch {
        return watch::run_watch_mode(cli).await;
    }

    if cli.voice {
        #[cfg(feature = "voice")]
        return voice::run_voice_loop(cli).await;

        #[cfg(not(feature = "voice"))]
        return Err(eyre!(
            "Voice input requires the 'voice' feature. Rebuild with: cargo build --features voice"
        ));
    }

    let goal = cli
        .goal
        .as_ref()
        .ok_or_else(|| eyre!("No goal provided. Usage: ava \"your goal here\""))?
        .clone();

    single::run_single_agent(cli, &goal).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_agent::message_queue::MessageQueue;
    use ava_types::MessageTier;
    use std::path::Path;

    #[test]
    fn watcher_detects_comment_directive_lines() {
        let content = "// ava: fix this function\n# ava: add tests\n-- ava: update docs";
        let directives = watch::extract_comment_directives(content);
        assert_eq!(
            directives,
            vec![
                "fix this function".to_string(),
                "add tests".to_string(),
                "update docs".to_string(),
            ]
        );
    }

    #[test]
    fn comment_directive_ignores_non_directive_lines() {
        let content =
            "// TODO: not a trigger\nlet x = 1;\n#ava missing space\n* ava: markdown list";
        let directives = watch::extract_comment_directives(content);
        assert!(directives.is_empty());
    }

    #[test]
    fn watcher_ignore_paths_filters_internal_dirs() {
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/.git/config"
        )));
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/.ava/state.json"
        )));
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/target/debug/app"
        )));
        assert!(watch::should_ignore_watch_path(Path::new(
            "/repo/node_modules/pkg/index.js"
        )));
        assert!(!watch::should_ignore_watch_path(Path::new(
            "/repo/src/lib.rs"
        )));
    }

    #[test]
    fn watcher_trigger_event_kind_accepts_modify_and_create() {
        use notify::event::{CreateKind, EventKind, ModifyKind};

        assert!(watch::is_trigger_event_kind(&EventKind::Modify(
            ModifyKind::Any
        )));
        assert!(watch::is_trigger_event_kind(&EventKind::Create(
            CreateKind::File
        )));
        assert!(!watch::is_trigger_event_kind(&EventKind::Create(
            CreateKind::Folder
        )));
    }

    #[test]
    fn test_parse_steering_with_bang_prefix() {
        let msg = input::parse_stdin_message("!stop, use trait objects instead").unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "stop, use trait objects instead");
    }

    #[test]
    fn test_parse_plain_text_defaults_to_steering() {
        let msg = input::parse_stdin_message("do something different").unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "do something different");
    }

    #[test]
    fn test_parse_follow_up_with_gt_prefix() {
        let msg = input::parse_stdin_message(">also check the tests when done").unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "also check the tests when done");
    }

    #[test]
    fn test_parse_follow_up_with_space() {
        let msg = input::parse_stdin_message("> run tests after").unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "run tests after");
    }

    #[test]
    fn test_parse_post_complete_without_group() {
        let msg = input::parse_stdin_message(">>review the final code").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 1 });
        assert_eq!(msg.text, "review the final code");
    }

    #[test]
    fn test_parse_post_complete_with_group() {
        let msg = input::parse_stdin_message(">>2 commit everything").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 2 });
        assert_eq!(msg.text, "commit everything");
    }

    #[test]
    fn test_parse_post_complete_group_with_spaces() {
        let msg = input::parse_stdin_message(">>  3  final review").unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 3 });
        assert_eq!(msg.text, "final review");
    }

    #[test]
    fn test_parse_empty_line_returns_none() {
        assert!(input::parse_stdin_message("").is_none());
        assert!(input::parse_stdin_message("   ").is_none());
    }

    #[test]
    fn test_parse_empty_after_prefix_returns_none() {
        assert!(input::parse_stdin_message("!").is_none());
        assert!(input::parse_stdin_message("! ").is_none());
        assert!(input::parse_stdin_message(">").is_none());
        assert!(input::parse_stdin_message(">>").is_none());
    }

    #[test]
    fn test_parse_json_steering() {
        let msg =
            input::parse_json_stdin_message(r#"{"tier": "steering", "text": "change approach"}"#)
                .unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
        assert_eq!(msg.text, "change approach");
    }

    #[test]
    fn test_parse_json_follow_up() {
        let msg = input::parse_json_stdin_message(r#"{"tier": "follow-up", "text": "run tests"}"#)
            .unwrap();
        assert_eq!(msg.tier, MessageTier::FollowUp);
        assert_eq!(msg.text, "run tests");
    }

    #[test]
    fn test_parse_json_post_complete_with_group() {
        let msg = input::parse_json_stdin_message(
            r#"{"tier": "post-complete", "text": "commit", "group": 3}"#,
        )
        .unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 3 });
        assert_eq!(msg.text, "commit");
    }

    #[test]
    fn test_parse_json_defaults_group_to_1() {
        let msg = input::parse_json_stdin_message(r#"{"tier": "post-complete", "text": "review"}"#)
            .unwrap();
        assert_eq!(msg.tier, MessageTier::PostComplete { group: 1 });
    }

    #[test]
    fn test_parse_json_defaults_tier_to_steering() {
        let msg = input::parse_json_stdin_message(r#"{"text": "urgent change"}"#).unwrap();
        assert_eq!(msg.tier, MessageTier::Steering);
    }

    #[test]
    fn test_parse_json_empty_text_returns_none() {
        assert!(input::parse_json_stdin_message(r#"{"tier": "steering", "text": ""}"#).is_none());
    }

    #[test]
    fn test_parse_json_invalid_json_returns_none() {
        assert!(input::parse_json_stdin_message("not json at all").is_none());
    }

    #[test]
    fn test_parse_json_missing_text_returns_none() {
        assert!(input::parse_json_stdin_message(r#"{"tier": "steering"}"#).is_none());
    }

    #[test]
    fn test_populate_follow_up_from_cli() {
        let cli = CliArgs {
            follow_up: vec!["run tests".to_string(), "check compilation".to_string()],
            later: vec![],
            later_group: vec![],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();
        assert_eq!(queue.pending_count(), (0, 2, 0));
        let msgs = queue.drain_follow_up();
        assert_eq!(msgs, vec!["run tests", "check compilation"]);
    }

    #[test]
    fn test_populate_later_auto_groups() {
        let cli = CliArgs {
            follow_up: vec![],
            later: vec!["review code".to_string(), "commit if clean".to_string()],
            later_group: vec![],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();
        assert_eq!(queue.pending_count(), (0, 0, 2));

        let (g1, msgs1) = queue.next_post_complete_group().unwrap();
        assert_eq!(g1, 1);
        assert_eq!(msgs1, vec!["review code"]);

        let (g2, msgs2) = queue.next_post_complete_group().unwrap();
        assert_eq!(g2, 2);
        assert_eq!(msgs2, vec!["commit if clean"]);
    }

    #[test]
    fn test_populate_later_group_explicit() {
        let cli = CliArgs {
            follow_up: vec![],
            later: vec![],
            later_group: vec![
                "3".to_string(),
                "final step".to_string(),
                "1".to_string(),
                "first step".to_string(),
            ],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();

        let (g1, msgs1) = queue.next_post_complete_group().unwrap();
        assert_eq!(g1, 1);
        assert_eq!(msgs1, vec!["first step"]);

        let (g3, msgs3) = queue.next_post_complete_group().unwrap();
        assert_eq!(g3, 3);
        assert_eq!(msgs3, vec!["final step"]);
    }

    #[test]
    fn test_populate_mixed_flags() {
        let cli = CliArgs {
            follow_up: vec!["follow".to_string()],
            later: vec!["later".to_string()],
            later_group: vec!["5".to_string(), "explicit".to_string()],
            ..default_cli()
        };
        let (mut queue, tx) = MessageQueue::new();
        input::populate_queue_from_cli(&cli, &tx);
        drop(tx);
        queue.poll();
        assert_eq!(queue.pending_count(), (0, 1, 2));
    }

    fn default_cli() -> CliArgs {
        use clap::Parser;
        CliArgs::parse_from([
            "ava",
            "test-goal",
            "--headless",
            "--provider",
            "mock",
            "--model",
            "test",
        ])
    }
}
