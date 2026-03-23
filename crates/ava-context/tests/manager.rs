use ava_context::ContextManager;
use ava_types::{Message, Role, ToolResult};

#[test]
fn add_and_get_messages() {
    let mut manager = ContextManager::new(2_000);
    manager.add_message(Message::new(Role::System, "system prompt"));
    manager.add_message(Message::new(Role::User, "hello"));

    assert_eq!(manager.get_messages().len(), 2);
    assert_eq!(manager.get_messages()[1].content, "hello");
}

#[test]
fn token_count_increases_when_messages_added() {
    let mut manager = ContextManager::new(2_000);
    manager.add_message(Message::new(Role::User, "count my tokens"));

    assert!(manager.token_count() > 0);
}

#[test]
fn should_compact_at_eighty_percent_threshold() {
    let mut manager = ContextManager::new(50);
    // Use multi-word content so word-based token estimator counts enough tokens
    let words = (0..60)
        .map(|i| format!("word{i}"))
        .collect::<Vec<_>>()
        .join(" ");
    manager.add_message(Message::new(Role::User, words));

    assert!(manager.should_compact());
}

#[test]
fn compact_reduces_message_set_when_over_limit() {
    let mut manager = ContextManager::new(40);

    for i in 0..8 {
        let words = (0..20)
            .map(|j| format!("w{i}_{j}"))
            .collect::<Vec<_>>()
            .join(" ");
        manager.add_message(Message::new(Role::User, words));
    }

    let before = manager.get_messages().len();
    manager.compact().expect("compaction should succeed");
    let after = manager.get_messages().len();

    assert!(after < before);
}

#[test]
fn add_tool_result_creates_tool_message() {
    let mut manager = ContextManager::new(2_000);
    manager.add_tool_result(ToolResult {
        call_id: "call_1".to_string(),
        content: "tool output".to_string(),
        is_error: false,
    });

    assert_eq!(manager.get_messages().len(), 1);
    assert_eq!(manager.get_messages()[0].role, Role::Tool);
    assert_eq!(manager.get_messages()[0].tool_results.len(), 1);
}

#[test]
fn get_system_message_returns_system_message_when_present() {
    let mut manager = ContextManager::new(2_000);
    manager.add_message(Message::new(Role::User, "hello"));
    manager.add_message(Message::new(Role::System, "system prompt"));

    let message = manager.get_system_message();
    assert!(message.is_some());
    assert_eq!(message.expect("system message").role, Role::System);
}

#[test]
fn get_system_message_returns_none_when_absent() {
    let mut manager = ContextManager::new(2_000);
    manager.add_message(Message::new(Role::User, "hello"));

    assert!(manager.get_system_message().is_none());
}

#[test]
fn compact_preserves_system_prompt() {
    let mut manager = ContextManager::new(40);

    // System prompt first
    manager.add_message(Message::new(Role::System, "You are a helpful assistant"));

    // Fill with enough messages to trigger compaction
    for i in 0..10 {
        let words = (0..20)
            .map(|j| format!("w{i}_{j}"))
            .collect::<Vec<_>>()
            .join(" ");
        manager.add_message(Message::new(Role::User, words));
    }

    assert!(manager.should_compact());
    manager.compact().expect("compaction should succeed");

    let messages = manager.get_messages();
    assert!(
        !messages.is_empty(),
        "should have messages after compaction"
    );
    assert_eq!(
        messages[0].role,
        Role::System,
        "first message should be System after compaction"
    );
    assert!(
        messages[0].content.contains("helpful assistant"),
        "system prompt content should be preserved"
    );
}

#[test]
fn compact_populates_compacted_messages() {
    let mut manager = ContextManager::new(40);

    manager.add_message(Message::new(Role::System, "You are a helpful assistant"));

    for i in 0..10 {
        let words = (0..20)
            .map(|j| format!("w{i}_{j}"))
            .collect::<Vec<_>>()
            .join(" ");
        manager.add_message(Message::new(Role::User, words));
    }

    let before_count = manager.get_messages().len();
    assert!(manager.should_compact());
    manager.compact().expect("compaction should succeed");

    let after_count = manager.get_messages().len();
    let compacted = manager.compacted_messages();

    // Some messages should have been compacted
    assert!(
        !compacted.is_empty(),
        "compacted messages should not be empty after compaction"
    );
    // Compacted messages should be marked correctly
    for msg in compacted {
        assert!(
            !msg.agent_visible,
            "compacted message should have agent_visible=false"
        );
        assert!(
            msg.user_visible,
            "compacted message should have user_visible=true"
        );
        assert!(
            msg.is_compacted(),
            "compacted message should return true for is_compacted()"
        );
        assert!(
            msg.original_content.is_some(),
            "compacted message should preserve original_content"
        );
    }
    // Total of active + compacted should account for all original messages
    // (some may have been genuinely new summary messages, so >=)
    assert!(
        after_count + compacted.len() >= before_count,
        "active ({after_count}) + compacted ({}) should cover original ({before_count})",
        compacted.len()
    );
}

#[test]
fn get_all_messages_for_ui_includes_compacted() {
    let mut manager = ContextManager::new(40);

    manager.add_message(Message::new(Role::System, "system"));

    for i in 0..10 {
        let words = (0..20)
            .map(|j| format!("w{i}_{j}"))
            .collect::<Vec<_>>()
            .join(" ");
        manager.add_message(Message::new(Role::User, words));
    }

    manager.compact().expect("compaction should succeed");

    let all_ui_messages = manager.get_all_messages_for_ui();
    let active_messages = manager.get_messages();
    let compacted = manager.compacted_messages();

    // UI view should include both active and compacted
    assert!(
        all_ui_messages.len() >= active_messages.len(),
        "UI messages should include at least the active messages"
    );
    assert_eq!(
        all_ui_messages.len(),
        active_messages.len() + compacted.len(),
        "UI messages should be active + compacted"
    );
}

#[test]
fn agent_visible_messages_exclude_compacted() {
    let mut manager = ContextManager::new(40);

    manager.add_message(Message::new(Role::System, "system"));

    for i in 0..10 {
        let words = (0..20)
            .map(|j| format!("w{i}_{j}"))
            .collect::<Vec<_>>()
            .join(" ");
        manager.add_message(Message::new(Role::User, words));
    }

    manager.compact().expect("compaction should succeed");

    let agent_visible = manager.get_agent_visible_messages();
    let active = manager.get_messages();

    // All active messages should be agent-visible (they haven't been individually marked)
    assert_eq!(
        agent_visible.len(),
        active.len(),
        "agent-visible count should match active messages"
    );
    for msg in &agent_visible {
        assert!(
            msg.agent_visible,
            "all returned messages should be agent_visible"
        );
    }
}
