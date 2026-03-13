use super::*;

#[test]
fn insert_newline_and_cursor_tracking() {
    let mut input = InputState::default();
    input.insert_str("hello");
    input.insert_char('\n');
    input.insert_str("world");

    assert_eq!(input.buffer, "hello\nworld");
    assert_eq!(input.cursor_line_col(), (1, 5)); // line 1, col 5
    assert!(input.is_multiline());
}

#[test]
fn move_up_down_across_lines() {
    let mut input = InputState::default();
    input.insert_str("abc\ndef\nghi");
    // Cursor at end: line 2, col 3
    assert_eq!(input.cursor_line_col(), (2, 3));

    assert!(input.move_up());
    assert_eq!(input.cursor_line_col(), (1, 3));

    assert!(input.move_up());
    assert_eq!(input.cursor_line_col(), (0, 3));

    // Already on first line
    assert!(!input.move_up());

    assert!(input.move_down());
    assert_eq!(input.cursor_line_col(), (1, 3));

    assert!(input.move_down());
    assert_eq!(input.cursor_line_col(), (2, 3));

    // Already on last line
    assert!(!input.move_down());
}

#[test]
fn move_up_clamps_column() {
    let mut input = InputState::default();
    input.insert_str("ab\nlong line\nxy");
    // line 2, col 2
    assert_eq!(input.cursor_line_col(), (2, 2));

    input.move_up(); // line 1, col 2
    assert_eq!(input.cursor_line_col(), (1, 2));

    input.move_end(); // col 9 ("long line")
    assert_eq!(input.cursor_line_col(), (1, 9));

    input.move_up(); // line 0 only has 2 chars, should clamp
    assert_eq!(input.cursor_line_col(), (0, 2));
}

#[test]
fn home_end_within_line() {
    let mut input = InputState::default();
    input.insert_str("first\nsecond\nthird");
    // cursor at end of "third" → line 2, col 5
    input.move_up(); // line 1
    input.move_home();
    assert_eq!(input.cursor_line_col(), (1, 0));

    input.move_end();
    assert_eq!(input.cursor_line_col(), (1, 6)); // "second" = 6 chars
}

#[test]
fn backspace_merges_lines() {
    let mut input = InputState::default();
    input.insert_str("hello\nworld");
    // Move to start of "world" (line 1, col 0)
    input.move_home();
    assert_eq!(input.cursor_line_col(), (1, 0));

    // Backspace should delete the '\n' and merge lines
    input.delete_backward();
    assert_eq!(input.buffer, "helloworld");
    assert_eq!(input.cursor_line_col(), (0, 5));
}

#[test]
fn delete_forward_merges_lines() {
    let mut input = InputState::default();
    input.insert_str("hello\nworld");
    // Move cursor to end of "hello" (just before '\n')
    input.cursor = 5;
    assert_eq!(input.cursor_line_col(), (0, 5));

    input.delete_forward();
    assert_eq!(input.buffer, "helloworld");
}

#[test]
fn single_line_up_down_returns_false() {
    let mut input = InputState::default();
    input.insert_str("hello");
    assert!(!input.move_up());
    assert!(!input.move_down());
}

#[test]
fn submit_preserves_newlines_in_content() {
    let mut input = InputState::default();
    input.insert_str("line1\nline2");
    let submitted = input.submit();
    assert_eq!(submitted, Some("line1\nline2".to_string()));
}

// --- Paste collapsing tests ---

#[test]
fn paste_small_text_inserts_directly() {
    let mut input = InputState::default();
    input.handle_paste("hello world".to_string());
    assert_eq!(input.buffer, "hello world");
    assert!(input.pending_pastes.is_empty());
}

#[test]
fn paste_below_both_thresholds_inserts_directly() {
    let mut input = InputState::default();
    // 4 lines, short text — below both thresholds
    input.handle_paste("a\nb\nc\nd".to_string());
    assert_eq!(input.buffer, "a\nb\nc\nd");
    assert!(input.pending_pastes.is_empty());
}

#[test]
fn paste_many_lines_collapses() {
    let mut input = InputState::default();
    let text = "line1\nline2\nline3\nline4\nline5\nline6";
    input.handle_paste(text.to_string());
    assert_eq!(input.buffer, "[Pasted Text: 6 lines]");
    assert_eq!(input.pending_pastes.len(), 1);
    assert_eq!(
        input.pending_pastes.get("[Pasted Text: 6 lines]"),
        Some(&text.to_string())
    );
}

#[test]
fn paste_long_single_line_collapses_by_chars() {
    let mut input = InputState::default();
    let text = "x".repeat(600);
    input.handle_paste(text.clone());
    assert_eq!(input.buffer, "[Pasted Text: 600 chars]");
    assert_eq!(input.pending_pastes.len(), 1);
    assert_eq!(
        input.pending_pastes.get("[Pasted Text: 600 chars]"),
        Some(&text)
    );
}

#[test]
fn paste_dedup_numbering() {
    let mut input = InputState::default();
    // Both have 6 lines → same description
    let text1 = "a\nb\nc\nd\ne\nf";
    let text2 = "g\nh\ni\nj\nk\nl";
    input.handle_paste(text1.to_string());
    input.handle_paste(text2.to_string());

    assert!(input.buffer.contains("[Pasted Text: 6 lines]"));
    assert!(input.buffer.contains("[Pasted Text: 6 lines #2]"));
    assert_eq!(input.pending_pastes.len(), 2);
    assert_eq!(
        input.pending_pastes.get("[Pasted Text: 6 lines]"),
        Some(&text1.to_string())
    );
    assert_eq!(
        input.pending_pastes.get("[Pasted Text: 6 lines #2]"),
        Some(&text2.to_string())
    );
}

#[test]
fn submit_expands_pastes() {
    let mut input = InputState::default();
    let text = "line1\nline2\nline3\nline4\nline5";
    input.handle_paste(text.to_string());
    input.insert_str(" and more");

    let submitted = input.submit().unwrap();
    assert!(submitted.contains("line1\nline2\nline3\nline4\nline5"));
    assert!(submitted.contains("and more"));
    // Placeholders should not appear in submitted text
    assert!(!submitted.contains("[Pasted Text:"));
}

#[test]
fn backspace_deletes_placeholder_atomically() {
    let mut input = InputState::default();
    let text = "a\nb\nc\nd\ne\nf";
    input.handle_paste(text.to_string());
    // Cursor is at end of placeholder
    assert!(input.buffer.starts_with("[Pasted Text:"));

    input.delete_backward_with_paste();
    assert_eq!(input.buffer, "");
    assert!(input.pending_pastes.is_empty());
    assert_eq!(input.cursor, 0);
}

#[test]
fn backspace_normal_when_not_on_placeholder() {
    let mut input = InputState::default();
    input.insert_str("hello");
    input.delete_backward_with_paste();
    assert_eq!(input.buffer, "hell");
}

#[test]
fn expand_pastes_in_buffer() {
    let mut input = InputState::default();
    let text = "line1\nline2\nline3\nline4\nline5";
    input.handle_paste(text.to_string());
    let expanded = input.expand_pastes(&input.buffer.clone());
    assert_eq!(expanded, text);
}

#[test]
fn toggle_paste_expansion() {
    let mut input = InputState::default();
    let text = "line1\nline2\nline3\nline4\nline5";
    input.handle_paste(text.to_string());
    assert!(input.buffer.starts_with("[Pasted Text:"));

    let toggled = input.toggle_paste_expansion();
    assert!(toggled);
    assert_eq!(input.buffer, text);
    assert!(input.pending_pastes.is_empty());
}

#[test]
fn clear_resets_paste_state() {
    let mut input = InputState::default();
    input.handle_paste("a\nb\nc\nd\ne\nf".to_string());
    assert!(!input.pending_pastes.is_empty());

    input.clear();
    assert!(input.pending_pastes.is_empty());
    assert!(input.paste_counter.is_empty());
    assert!(input.buffer.is_empty());
}

#[test]
fn paste_with_prefix_text() {
    let mut input = InputState::default();
    input.insert_str("Please review: ");
    let text = "line1\nline2\nline3\nline4\nline5";
    input.handle_paste(text.to_string());

    assert!(input.buffer.starts_with("Please review: [Pasted Text:"));
    let submitted = input.submit().unwrap();
    assert!(submitted.starts_with("Please review: line1\nline2"));
}

// --- @-mention / attachment tests ---

#[test]
fn at_triggers_mention_autocomplete() {
    let mut input = InputState::default();
    input.insert_char('@');
    // Should trigger AtMention autocomplete (items may be empty if no files match)
    assert!(matches!(
        input.autocomplete,
        Some(ref ac) if ac.trigger == AutocompleteTrigger::AtMention
    ));
}

#[test]
fn add_and_remove_attachment() {
    let mut input = InputState::default();
    let attachment = ava_types::ContextAttachment::File {
        path: std::path::PathBuf::from("src/main.rs"),
    };
    input.add_attachment(attachment.clone());
    assert_eq!(input.attachments.len(), 1);
    assert_eq!(input.attachments[0], attachment);

    input.remove_attachment(0);
    assert!(input.attachments.is_empty());
}

#[test]
fn clear_resets_attachments() {
    let mut input = InputState::default();
    input.add_attachment(ava_types::ContextAttachment::File {
        path: std::path::PathBuf::from("test.rs"),
    });
    assert!(!input.attachments.is_empty());
    input.clear();
    assert!(input.attachments.is_empty());
}

#[test]
fn has_mention_autocomplete_when_at_typed() {
    let mut input = InputState::default();
    input.insert_char('@');
    // Even if no files match the empty query, the autocomplete should be AtMention type
    let is_at = matches!(
        input.autocomplete,
        Some(ref ac) if ac.trigger == AutocompleteTrigger::AtMention
    );
    assert!(is_at);
}

#[test]
fn submit_clears_attachments() {
    let mut input = InputState::default();
    input.insert_str("hello");
    input.add_attachment(ava_types::ContextAttachment::File {
        path: std::path::PathBuf::from("test.rs"),
    });
    let _ = input.submit();
    assert!(input.attachments.is_empty());
}

// --- Mention file cache tests ---

#[test]
fn mention_cache_populated_on_first_at() {
    let mut input = InputState::default();
    input.insert_char('@');
    // After the first @, the cache should be populated (cwd set).
    assert!(
        input.mention_cache.cwd.is_some(),
        "cache cwd should be set after first @ trigger"
    );
}

#[test]
fn mention_cache_reused_on_subsequent_keystrokes() {
    let mut input = InputState::default();
    input.insert_char('@');
    // Snapshot the cache state after first scan
    let cwd_after_first = input.mention_cache.cwd.clone();
    let items_len_first = input.mention_cache.items.len();

    // Type more characters — should reuse cache, not rescan
    input.insert_char('s');
    assert_eq!(
        input.mention_cache.cwd, cwd_after_first,
        "cache cwd should not change on refinement keystroke"
    );
    assert_eq!(
        input.mention_cache.items.len(),
        items_len_first,
        "cache item count should stay the same (filter is in AutocompleteState, not cache)"
    );

    input.insert_char('r');
    assert_eq!(input.mention_cache.cwd, cwd_after_first);
    assert_eq!(input.mention_cache.items.len(), items_len_first);
}

#[test]
fn mention_cache_invalidated_on_clear() {
    let mut input = InputState::default();
    input.insert_char('@');
    assert!(input.mention_cache.cwd.is_some());

    input.clear();
    assert!(
        input.mention_cache.cwd.is_none(),
        "cache should be invalidated after clear()"
    );
    assert!(input.mention_cache.items.is_empty());
}

#[test]
fn mention_cache_invalidated_when_leaving_at_context() {
    let mut input = InputState::default();
    input.insert_char('@');
    assert!(input.mention_cache.cwd.is_some());

    // Simulate clearing buffer and typing a non-@ token
    input.buffer.clear();
    input.cursor = 0;
    input.insert_str("hello");
    // After typing a plain word, the else branch fires and invalidates cache
    assert!(
        input.mention_cache.cwd.is_none(),
        "cache should be invalidated when no longer in @ context"
    );
}

#[test]
fn mention_cache_invalidated_on_dismiss() {
    let mut input = InputState::default();
    input.insert_char('@');
    assert!(input.mention_cache.cwd.is_some());

    input.dismiss_autocomplete();
    assert!(
        input.mention_cache.cwd.is_none(),
        "cache should be invalidated on dismiss_autocomplete"
    );
}

#[test]
fn dismiss_autocomplete_preserves_input_buffer() {
    let mut input = InputState::default();
    for ch in "/hel".chars() {
        input.insert_char(ch);
    }
    assert!(input.has_slash_autocomplete());

    input.dismiss_autocomplete();

    assert_eq!(input.buffer, "/hel");
    assert_eq!(input.cursor, 4);
    assert!(input.autocomplete.is_none());
}

#[test]
fn mention_cache_respects_folders_only_mode_switch() {
    let mut input = InputState::default();

    // Start with general @ (folders_only = false)
    input.insert_char('@');
    assert!(!input.mention_cache.folders_only);
    let items_general = input.mention_cache.items.len();

    // Clear and switch to folder: prefix
    input.buffer.clear();
    input.cursor = 0;
    input.mention_cache.invalidate();
    input.insert_str("@folder:");
    assert!(
        input.mention_cache.folders_only,
        "cache should reflect folders_only = true for @folder: prefix"
    );
    // The folder-only scan should have <= items compared to general
    assert!(input.mention_cache.items.len() <= items_general);
}

#[test]
fn mention_cache_query_filtering_preserves_full_cache() {
    let mut input = InputState::default();
    input.insert_char('@');
    let full_cache_len = input.mention_cache.items.len();

    // Type a query that likely filters down the visible items
    input.insert_str("zzz_unlikely_match");

    // The cache itself should still hold all items (filtering is in AutocompleteState)
    assert_eq!(
        input.mention_cache.items.len(),
        full_cache_len,
        "cache should hold all items; filtering happens in AutocompleteState"
    );

    // But the autocomplete visible items should be filtered (possibly empty)
    if let Some(ref ac) = input.autocomplete {
        assert!(
            ac.items.len() <= full_cache_len,
            "autocomplete items should be filtered subset of cache"
        );
    }
}

#[test]
fn mention_cache_codebase_prefix_does_not_use_cache() {
    let mut input = InputState::default();
    input.insert_str("@codebase:query");

    // codebase: prefix should not populate the file cache
    // (it uses a synthetic single item, not file scanning)
    assert!(
        input.mention_cache.cwd.is_none(),
        "codebase: queries should not populate the file scan cache"
    );
}

#[test]
fn mention_cache_survives_backspace_within_at_session() {
    let mut input = InputState::default();
    input.insert_str("@src");
    let cwd_snapshot = input.mention_cache.cwd.clone();
    let cache_len = input.mention_cache.items.len();
    assert!(cwd_snapshot.is_some());

    // Backspace one char — still in @ context
    input.delete_backward();
    assert_eq!(input.buffer, "@sr");
    assert_eq!(
        input.mention_cache.cwd, cwd_snapshot,
        "cache should survive backspace within @ session"
    );
    assert_eq!(input.mention_cache.items.len(), cache_len);

    // Backspace again
    input.delete_backward();
    assert_eq!(input.buffer, "@s");
    assert_eq!(input.mention_cache.cwd, cwd_snapshot);

    // Backspace to just "@"
    input.delete_backward();
    assert_eq!(input.buffer, "@");
    assert_eq!(input.mention_cache.cwd, cwd_snapshot);
}

#[test]
fn mention_cache_invalidated_when_at_deleted() {
    let mut input = InputState::default();
    input.insert_char('@');
    assert!(input.mention_cache.cwd.is_some());

    // Delete the @ itself
    input.delete_backward();
    assert_eq!(input.buffer, "");
    assert!(
        input.mention_cache.cwd.is_none(),
        "cache should be invalidated when @ is deleted"
    );
}

#[test]
fn slash_autocomplete_does_not_populate_mention_cache() {
    let mut input = InputState::default();
    input.insert_char('/');
    assert!(
        input.mention_cache.cwd.is_none(),
        "slash commands should not populate mention file cache"
    );
    assert!(input.mention_cache.items.is_empty());
}
