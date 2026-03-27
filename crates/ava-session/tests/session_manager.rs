use ava_session::SessionManager;
use ava_types::{ExternalSessionLink, Message, Role, StructuredContentBlock};

fn temp_db_path(test_name: &str) -> std::path::PathBuf {
    std::env::temp_dir().join(format!(
        "ava-session-{test_name}-{}.sqlite",
        uuid::Uuid::new_v4()
    ))
}

#[test]
fn create_and_get_session() {
    let manager = SessionManager::new(temp_db_path("create_get")).expect("manager should init");
    let mut session = manager.create().expect("create should work");
    session.add_message(Message::new(Role::User, "hello"));
    manager.save(&session).expect("save should work");

    let found = manager
        .get(session.id)
        .expect("get should succeed")
        .expect("session should exist");
    assert_eq!(found.id, session.id);
    assert_eq!(found.messages.len(), 1);
}

#[test]
fn list_recent_orders_by_updated_at() {
    let manager = SessionManager::new(temp_db_path("list_recent")).expect("manager should init");
    let mut first = manager.create().expect("create should work");
    first.add_message(Message::new(Role::User, "first"));
    manager.save(&first).expect("save should work");

    let mut second = manager.create().expect("create should work");
    second.add_message(Message::new(Role::User, "second"));
    manager.save(&second).expect("save should work");

    let recent = manager.list_recent(2).expect("list_recent should work");
    assert_eq!(recent.len(), 2);
    assert_eq!(recent[0].id, second.id);
}

#[test]
fn fork_preserves_messages_and_sets_parent() {
    let manager = SessionManager::new(temp_db_path("fork")).expect("manager should init");
    let mut base = manager.create().expect("create should work");
    base.add_message(Message::new(Role::User, "base"));

    let forked = manager.fork(&base).expect("fork should work");
    manager.save(&forked).expect("forked save should work");
    let loaded = manager
        .get(forked.id)
        .expect("forked get should work")
        .expect("forked session should exist");

    assert_eq!(forked.messages.len(), 1);
    assert_eq!(forked.messages[0].content, "base");
    assert_eq!(
        loaded.metadata["parent_id"].as_str(),
        Some(base.id.to_string().as_str())
    );
}

#[test]
fn search_finds_sessions_by_message_content() {
    let manager = SessionManager::new(temp_db_path("search")).expect("manager should init");
    let mut session = manager.create().expect("create should work");
    session.add_message(Message::new(Role::User, "implement mcp search"));
    manager.save(&session).expect("save should work");

    let matches = manager.search("mcp").expect("search should work");
    assert_eq!(matches.len(), 1);
    assert_eq!(matches[0].id, session.id);
}

#[test]
fn delete_removes_session() {
    let manager = SessionManager::new(temp_db_path("delete")).expect("manager should init");
    let mut session = manager.create().expect("create should work");
    session.add_message(Message::new(Role::User, "to be deleted"));
    manager.save(&session).expect("save should work");

    manager.delete(session.id).expect("delete should work");
    let found = manager.get(session.id).expect("get should work");
    assert!(found.is_none());

    let error = manager
        .delete(session.id)
        .expect_err("second delete should fail");
    assert!(error.to_string().contains("not found"));
}

#[test]
fn save_load_roundtrip_preserves_tool_calls() {
    let manager =
        SessionManager::new(temp_db_path("roundtrip_tools")).expect("manager should init");
    let mut session = manager.create().expect("create should work");

    let tc = ava_types::ToolCall {
        id: "call_42".to_string(),
        name: "read".to_string(),
        arguments: serde_json::json!({"path": "/tmp/test.rs"}),
    };
    let mut assistant_msg = Message::new(Role::Assistant, "Reading file");
    assistant_msg.tool_calls = vec![tc.clone()];
    session.add_message(assistant_msg);

    let mut tool_msg = Message::new(Role::Tool, "file contents here");
    tool_msg.tool_results = vec![ava_types::ToolResult {
        call_id: "call_42".to_string(),
        content: "file contents here".to_string(),
        is_error: false,
    }];
    session.add_message(tool_msg);

    manager.save(&session).expect("save should work");

    let loaded = manager
        .get(session.id)
        .expect("get should succeed")
        .expect("session should exist");

    assert_eq!(loaded.messages.len(), 2);
    assert_eq!(loaded.messages[0].tool_calls.len(), 1);
    assert_eq!(loaded.messages[0].tool_calls[0].id, "call_42");
    assert_eq!(loaded.messages[0].tool_calls[0].name, "read");
    assert_eq!(loaded.messages[1].tool_results.len(), 1);
    assert_eq!(loaded.messages[1].tool_results[0].call_id, "call_42");
    assert!(!loaded.messages[1].tool_results[0].is_error);
}

#[test]
fn list_recent_with_many_sessions() {
    let manager = SessionManager::new(temp_db_path("many_sessions")).expect("manager should init");

    for i in 0..100 {
        let mut session = manager.create().expect("create should work");
        session.add_message(Message::new(Role::User, format!("session {i}")));
        manager.save(&session).expect("save should work");
    }

    let recent = manager.list_recent(10).expect("list_recent should work");
    assert_eq!(recent.len(), 10);

    // Should be most recent first
    assert!(recent[0].messages[0].content.contains("session 99"));
}

#[test]
fn save_load_roundtrip_preserves_structured_external_fields() {
    let manager =
        SessionManager::new(temp_db_path("roundtrip_structured")).expect("manager should init");
    let mut session = manager.create().expect("create should work");

    let mut message = Message::new(Role::Assistant, "hello").with_structured_content(vec![
        StructuredContentBlock::Text {
            text: "hello".to_string(),
        },
        StructuredContentBlock::ToolUse {
            id: "tool-1".to_string(),
            name: "read".to_string(),
            input: serde_json::json!({"path": "src/main.rs"}),
        },
    ]);
    message.agent_visible = false;
    message.user_visible = false;
    message.original_content = Some("original hello".to_string());
    message.metadata = serde_json::json!({"source":"external-agent"});
    session.add_message(message);

    manager.save(&session).expect("save should work");
    let loaded = manager
        .get(session.id)
        .expect("get should succeed")
        .expect("session should exist");

    let loaded_msg = &loaded.messages[0];
    assert!(!loaded_msg.agent_visible);
    assert!(!loaded_msg.user_visible);
    assert_eq!(
        loaded_msg.original_content.as_deref(),
        Some("original hello")
    );
    assert_eq!(loaded_msg.structured_content.len(), 2);
    assert_eq!(loaded_msg.metadata["source"], "external-agent");
}

#[test]
fn find_recent_child_by_external_link_matches_latest_session() {
    let manager = SessionManager::new(temp_db_path("recent_child")).expect("manager should init");
    let parent = manager.create().expect("create should work");
    manager.save(&parent).expect("save should work");

    for external_id in ["sess-1", "sess-2"] {
        let mut child = manager.create().expect("create should work");
        child.metadata["parent_id"] = serde_json::json!(parent.id.to_string());
        child.metadata["agent_type"] = serde_json::json!("review");
        child.metadata["externalLink"] = serde_json::to_value(ExternalSessionLink {
            provider: Some("opencode".to_string()),
            agent_name: Some("opencode".to_string()),
            external_session_id: Some(external_id.to_string()),
            resume_attempted: true,
            resumed: true,
            model: None,
            cwd: Some("/tmp/project".to_string()),
        })
        .unwrap();
        manager.save(&child).expect("save child should work");
    }

    let found = manager
        .find_recent_child_by_external_link(parent.id, "review", "opencode", "/tmp/project")
        .expect("lookup should work")
        .expect("child should exist");

    let found_link: ExternalSessionLink =
        serde_json::from_value(found.metadata["externalLink"].clone()).unwrap();
    assert_eq!(found_link.external_session_id.as_deref(), Some("sess-2"));
}

#[test]
fn recent_delegation_records_returns_latest_first() {
    let manager = SessionManager::new(temp_db_path("delegation_records")).expect("manager");
    let parent = manager.create().expect("create parent");
    manager.save(&parent).expect("save parent");

    for outcome in ["error", "success"] {
        let mut child = manager.create().expect("create child");
        child.metadata["parent_id"] = serde_json::json!(parent.id.to_string());
        child.metadata["delegation"] = serde_json::json!({
            "agentType": "review",
            "provider": "opencode",
            "outcome": outcome,
        });
        manager.save(&child).expect("save child");
    }

    let records = manager
        .recent_delegation_records(parent.id, 5)
        .expect("records should load");

    assert_eq!(records.len(), 2);
    assert_eq!(records[0].outcome.as_deref(), Some("success"));
    assert_eq!(records[1].outcome.as_deref(), Some("error"));
}
