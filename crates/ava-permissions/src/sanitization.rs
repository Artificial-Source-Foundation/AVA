//! Unicode sanitization for input strings.
//!
//! Applies NFKC normalization and strips dangerous Unicode categories that could
//! be used for prompt injection, invisible text attacks, or bidirectional text exploits.

use unicode_normalization::UnicodeNormalization;

/// Sanitize a Unicode string by applying NFKC normalization and stripping dangerous characters.
///
/// Removes:
/// - Unicode category Cf (format controls): zero-width spaces (U+200B-U+200F),
///   bidirectional marks (U+202A-U+202E, U+2066-U+2069)
/// - Unicode category Co (private use): U+E000-U+F8FF
/// - BOM: U+FEFF
///
/// The process is iterative (up to 10 rounds) to handle cases where normalization
/// produces new strippable characters.
pub fn sanitize_unicode(input: &str) -> String {
    let mut result = input.to_string();

    for _ in 0..10 {
        // Apply NFKC normalization
        let normalized: String = result.nfkc().collect();

        // Strip dangerous characters
        let stripped: String = normalized.chars().filter(|c| !should_strip(*c)).collect();

        if stripped == result {
            // Stable — no further changes
            return stripped;
        }
        result = stripped;
    }

    result
}

/// Returns true if the character should be stripped during sanitization.
fn should_strip(c: char) -> bool {
    let cp = c as u32;

    // BOM (U+FEFF)
    if cp == 0xFEFF {
        return true;
    }

    // Category Cf: format control characters
    // Zero-width and joining controls (U+200B-U+200F)
    if (0x200B..=0x200F).contains(&cp) {
        return true;
    }

    // Bidirectional formatting (U+202A-U+202E)
    if (0x202A..=0x202E).contains(&cp) {
        return true;
    }

    // Bidirectional isolates (U+2066-U+2069)
    if (0x2066..=0x2069).contains(&cp) {
        return true;
    }

    // Soft hyphen (U+00AD) — invisible format control
    if cp == 0x00AD {
        return true;
    }

    // Word joiner (U+2060) and other zero-width chars
    if cp == 0x2060 {
        return true;
    }

    // Zero-width no-break space alias / function selector
    // U+FFF9-U+FFFB (interlinear annotation anchors)
    if (0xFFF9..=0xFFFB).contains(&cp) {
        return true;
    }

    // Category Co: private use area (U+E000-U+F8FF)
    if (0xE000..=0xF8FF).contains(&cp) {
        return true;
    }

    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normal_text_unchanged() {
        assert_eq!(sanitize_unicode("hello world"), "hello world");
    }

    #[test]
    fn ascii_unchanged() {
        let input = "cargo test --workspace 2>&1 | head -5";
        assert_eq!(sanitize_unicode(input), input);
    }

    #[test]
    fn removes_zero_width_space() {
        let input = "hello\u{200B}world";
        assert_eq!(sanitize_unicode(input), "helloworld");
    }

    #[test]
    fn removes_zero_width_non_joiner() {
        let input = "he\u{200C}llo";
        assert_eq!(sanitize_unicode(input), "hello");
    }

    #[test]
    fn removes_zero_width_joiner() {
        let input = "he\u{200D}llo";
        assert_eq!(sanitize_unicode(input), "hello");
    }

    #[test]
    fn removes_bom() {
        let input = "\u{FEFF}hello";
        assert_eq!(sanitize_unicode(input), "hello");
    }

    #[test]
    fn removes_bom_mid_string() {
        let input = "hel\u{FEFF}lo";
        assert_eq!(sanitize_unicode(input), "hello");
    }

    #[test]
    fn removes_bidi_marks() {
        let input = "a\u{202A}b\u{202B}c\u{202C}d\u{202D}e\u{202E}f";
        assert_eq!(sanitize_unicode(input), "abcdef");
    }

    #[test]
    fn removes_bidi_isolates() {
        let input = "a\u{2066}b\u{2067}c\u{2068}d\u{2069}e";
        assert_eq!(sanitize_unicode(input), "abcde");
    }

    #[test]
    fn removes_left_to_right_mark() {
        let input = "hello\u{200E}world";
        assert_eq!(sanitize_unicode(input), "helloworld");
    }

    #[test]
    fn removes_right_to_left_mark() {
        let input = "hello\u{200F}world";
        assert_eq!(sanitize_unicode(input), "helloworld");
    }

    #[test]
    fn removes_private_use_chars() {
        let input = "hello\u{E000}world\u{F8FF}end";
        assert_eq!(sanitize_unicode(input), "helloworldend");
    }

    #[test]
    fn nfkc_normalization_fullwidth() {
        // Fullwidth 'A' (U+FF21) should normalize to regular 'A'
        let input = "\u{FF21}BC";
        assert_eq!(sanitize_unicode(input), "ABC");
    }

    #[test]
    fn nfkc_normalization_ligature() {
        // fi ligature (U+FB01) should normalize to "fi"
        let input = "\u{FB01}le";
        assert_eq!(sanitize_unicode(input), "file");
    }

    #[test]
    fn preserves_legitimate_unicode() {
        // CJK, emoji, accented chars should be preserved
        assert_eq!(sanitize_unicode("caf\u{00E9}"), "caf\u{00E9}");
        assert_eq!(sanitize_unicode("\u{4F60}\u{597D}"), "\u{4F60}\u{597D}");
    }

    #[test]
    fn multiple_strippable_chars() {
        let input = "\u{FEFF}\u{200B}\u{200C}\u{200D}\u{200E}\u{200F}clean";
        assert_eq!(sanitize_unicode(input), "clean");
    }

    #[test]
    fn empty_string() {
        assert_eq!(sanitize_unicode(""), "");
    }

    #[test]
    fn iterative_stabilization() {
        // Even pathological input should stabilize
        let input = "\u{200B}\u{FEFF}\u{200B}ok\u{200B}\u{FEFF}";
        assert_eq!(sanitize_unicode(input), "ok");
    }
}
