//! Semantic input parsing for tool arguments.
//!
//! Converts natural-language numbers and boolean strings into their typed
//! equivalents so that LLM-produced arguments like `"twenty"` or `"yes"` are
//! handled gracefully.

use serde_json::Value;

/// Parse a JSON value as an integer, accepting both numeric values and
/// English number words (zero through twenty, thirty, forty, fifty, hundred,
/// thousand).
pub fn semantic_number(value: &Value) -> Option<i64> {
    match value {
        Value::Number(n) => n.as_i64(),
        Value::String(s) => parse_number_word(s.trim()),
        _ => None,
    }
}

/// Parse a JSON value as a boolean, accepting both boolean values and
/// common affirmative/negative strings (case-insensitive).
pub fn semantic_boolean(value: &Value) -> Option<bool> {
    match value {
        Value::Bool(b) => Some(*b),
        Value::String(s) => parse_boolean_word(s.trim()),
        _ => None,
    }
}

fn parse_number_word(s: &str) -> Option<i64> {
    // Try numeric parse first
    if let Ok(n) = s.parse::<i64>() {
        return Some(n);
    }

    match s.to_lowercase().as_str() {
        "zero" => Some(0),
        "one" => Some(1),
        "two" => Some(2),
        "three" => Some(3),
        "four" => Some(4),
        "five" => Some(5),
        "six" => Some(6),
        "seven" => Some(7),
        "eight" => Some(8),
        "nine" => Some(9),
        "ten" => Some(10),
        "eleven" => Some(11),
        "twelve" => Some(12),
        "thirteen" => Some(13),
        "fourteen" => Some(14),
        "fifteen" => Some(15),
        "sixteen" => Some(16),
        "seventeen" => Some(17),
        "eighteen" => Some(18),
        "nineteen" => Some(19),
        "twenty" => Some(20),
        "thirty" => Some(30),
        "forty" => Some(40),
        "fifty" => Some(50),
        "hundred" => Some(100),
        "thousand" => Some(1000),
        _ => None,
    }
}

fn parse_boolean_word(s: &str) -> Option<bool> {
    match s.to_lowercase().as_str() {
        "yes" | "true" | "on" | "enabled" | "1" => Some(true),
        "no" | "false" | "off" | "disabled" | "0" => Some(false),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn numeric_passthrough() {
        assert_eq!(semantic_number(&json!(42)), Some(42));
        assert_eq!(semantic_number(&json!(-7)), Some(-7));
        assert_eq!(semantic_number(&json!(0)), Some(0));
    }

    #[test]
    fn numeric_string_parse() {
        assert_eq!(semantic_number(&json!("123")), Some(123));
        assert_eq!(semantic_number(&json!("-5")), Some(-5));
    }

    #[test]
    fn word_numbers() {
        assert_eq!(semantic_number(&json!("zero")), Some(0));
        assert_eq!(semantic_number(&json!("one")), Some(1));
        assert_eq!(semantic_number(&json!("two")), Some(2));
        assert_eq!(semantic_number(&json!("three")), Some(3));
        assert_eq!(semantic_number(&json!("four")), Some(4));
        assert_eq!(semantic_number(&json!("five")), Some(5));
        assert_eq!(semantic_number(&json!("six")), Some(6));
        assert_eq!(semantic_number(&json!("seven")), Some(7));
        assert_eq!(semantic_number(&json!("eight")), Some(8));
        assert_eq!(semantic_number(&json!("nine")), Some(9));
        assert_eq!(semantic_number(&json!("ten")), Some(10));
        assert_eq!(semantic_number(&json!("eleven")), Some(11));
        assert_eq!(semantic_number(&json!("twelve")), Some(12));
        assert_eq!(semantic_number(&json!("thirteen")), Some(13));
        assert_eq!(semantic_number(&json!("fourteen")), Some(14));
        assert_eq!(semantic_number(&json!("fifteen")), Some(15));
        assert_eq!(semantic_number(&json!("sixteen")), Some(16));
        assert_eq!(semantic_number(&json!("seventeen")), Some(17));
        assert_eq!(semantic_number(&json!("eighteen")), Some(18));
        assert_eq!(semantic_number(&json!("nineteen")), Some(19));
        assert_eq!(semantic_number(&json!("twenty")), Some(20));
        assert_eq!(semantic_number(&json!("thirty")), Some(30));
        assert_eq!(semantic_number(&json!("forty")), Some(40));
        assert_eq!(semantic_number(&json!("fifty")), Some(50));
        assert_eq!(semantic_number(&json!("hundred")), Some(100));
        assert_eq!(semantic_number(&json!("thousand")), Some(1000));
    }

    #[test]
    fn word_numbers_case_insensitive() {
        assert_eq!(semantic_number(&json!("FIVE")), Some(5));
        assert_eq!(semantic_number(&json!("Twenty")), Some(20));
        assert_eq!(semantic_number(&json!("THOUSAND")), Some(1000));
    }

    #[test]
    fn unrecognized_number_returns_none() {
        assert_eq!(semantic_number(&json!("eleventy")), None);
        assert_eq!(semantic_number(&json!("a million")), None);
        assert_eq!(semantic_number(&json!(true)), None);
        assert_eq!(semantic_number(&json!(null)), None);
    }

    #[test]
    fn boolean_passthrough() {
        assert_eq!(semantic_boolean(&json!(true)), Some(true));
        assert_eq!(semantic_boolean(&json!(false)), Some(false));
    }

    #[test]
    fn boolean_truthy_strings() {
        for s in &["yes", "true", "on", "enabled", "1"] {
            assert_eq!(
                semantic_boolean(&json!(s)),
                Some(true),
                "expected true for {s}"
            );
        }
    }

    #[test]
    fn boolean_falsy_strings() {
        for s in &["no", "false", "off", "disabled", "0"] {
            assert_eq!(
                semantic_boolean(&json!(s)),
                Some(false),
                "expected false for {s}"
            );
        }
    }

    #[test]
    fn boolean_case_insensitive() {
        assert_eq!(semantic_boolean(&json!("YES")), Some(true));
        assert_eq!(semantic_boolean(&json!("No")), Some(false));
        assert_eq!(semantic_boolean(&json!("TRUE")), Some(true));
        assert_eq!(semantic_boolean(&json!("Disabled")), Some(false));
    }

    #[test]
    fn unrecognized_boolean_returns_none() {
        assert_eq!(semantic_boolean(&json!("maybe")), None);
        assert_eq!(semantic_boolean(&json!("yep")), None);
        assert_eq!(semantic_boolean(&json!(42)), None);
        assert_eq!(semantic_boolean(&json!(null)), None);
    }
}
