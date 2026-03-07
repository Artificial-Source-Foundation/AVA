use ava_tui::state::input::InputState;
use ava_tui::widgets::autocomplete::AutocompleteTrigger;

#[test]
fn input_insert_delete_and_submit() {
    let mut input = InputState::default();
    input.insert_char('h');
    input.insert_char('i');
    input.delete_backward();
    assert_eq!(input.buffer, "h");

    let submitted = input.submit().expect("should submit");
    assert_eq!(submitted, "h");
    assert_eq!(input.history.len(), 1);
}

#[test]
fn slash_triggers_autocomplete() {
    let mut input = InputState::default();
    input.insert_char('/');
    input.insert_char('h');
    let ac = input
        .autocomplete
        .as_ref()
        .expect("autocomplete should open");
    assert_eq!(ac.trigger, AutocompleteTrigger::Slash);
}
