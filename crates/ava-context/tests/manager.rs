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
    manager.add_message(Message::new(Role::User, "x".repeat(170)));

    assert!(manager.should_compact());
}

#[test]
fn compact_reduces_message_set_when_over_limit() {
    let mut manager = ContextManager::new(40);

    for _ in 0..8 {
        manager.add_message(Message::new(Role::User, "x".repeat(50)));
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
