use ava_tui::state::messages::{MessageKind, MessageState, UiMessage};

#[test]
fn message_state_scroll_and_push() {
    let mut state = MessageState::default();
    state.push(UiMessage::new(MessageKind::User, "hello"));
    assert_eq!(state.messages.len(), 1);

    state.scroll_up(5);
    assert!(!state.auto_scroll);
    state.scroll_down(5);
    assert!(state.auto_scroll);
}
