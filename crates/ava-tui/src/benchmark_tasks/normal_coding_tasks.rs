use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

pub fn normal_coding_tasks(_temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "normal_coding_retry_backoff",
            prompt: "Write a Rust function `next_retry_delay_ms(attempt: u32, base_delay_ms: u64, max_delay_ms: u64) -> u64` that uses exponential backoff. Attempt 0 returns the base delay. Each later attempt doubles the previous delay, capped at `max_delay_ms`. Only output the Rust code.".to_string(),
            expected_patterns: vec![r"fn\s+next_retry_delay_ms", r"min|clamp", r"attempt"],
            category: TaskCategory::NormalCoding,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn attempt_zero_uses_base_delay() {
        assert_eq!(next_retry_delay_ms(0, 100, 5_000), 100);
    }

    #[test]
    fn doubles_until_cap() {
        assert_eq!(next_retry_delay_ms(1, 100, 5_000), 200);
        assert_eq!(next_retry_delay_ms(4, 100, 5_000), 1_600);
    }

    #[test]
    fn respects_cap() {
        assert_eq!(next_retry_delay_ms(10, 100, 2_000), 2_000);
    }
}
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "normal_coding_port_parser",
            prompt: "Write a Rust function `parse_port(input: &str) -> Result<u16, String>` that trims the input, parses a decimal port, rejects empty strings, rejects values outside 1..=65535, and returns a helpful error string. Only output the Rust code.".to_string(),
            expected_patterns: vec![r"fn\s+parse_port", r"Result<u16,\s*String>", r"trim"],
            category: TaskCategory::NormalCoding,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_valid_port() {
        assert_eq!(parse_port(" 8080 ").unwrap(), 8080);
    }

    #[test]
    fn rejects_empty_input() {
        assert!(parse_port("   ").is_err());
    }

    #[test]
    fn rejects_zero() {
        assert!(parse_port("0").is_err());
    }

    #[test]
    fn rejects_out_of_range() {
        assert!(parse_port("70000").is_err());
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "normal_coding_markdown_toc",
            prompt: "Write a Rust function `build_toc(markdown: &str) -> Vec<(usize, String, String)>` that scans markdown headings beginning with `#`, returns `(level, title, slug)` tuples, trims heading text, and generates lowercase slugs with spaces replaced by `-`. Ignore non-heading lines. Only output the Rust code.".to_string(),
            expected_patterns: vec![r"fn\s+build_toc", r"Vec<\(usize, String, String\)>", r"starts_with"],
            category: TaskCategory::NormalCoding,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r##"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extracts_headings() {
        let toc = build_toc("# Intro\ntext\n## Next Steps\n");
        assert_eq!(toc.len(), 2);
        assert_eq!(toc[0], (1, "Intro".to_string(), "intro".to_string()));
        assert_eq!(toc[1], (2, "Next Steps".to_string(), "next-steps".to_string()));
    }

    #[test]
    fn ignores_non_headings() {
        let toc = build_toc("plain text\n- bullet\n");
        assert!(toc.is_empty());
    }
}
"##,
                setup_code: None,
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "normal_coding_json_merge",
            prompt: "Write a Rust function `merge_non_null(base: &mut serde_json::Value, patch: &serde_json::Value)` that recursively merges JSON objects but only overwrites fields when the patch value is not null. Arrays and scalars should be replaced wholesale. Only output the Rust code.".to_string(),
            expected_patterns: vec![r"serde_json::Value", r"Object|as_object", r"null"],
            category: TaskCategory::NormalCoding,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
use serde_json::json;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn merges_nested_objects_without_null_overwrite() {
        let mut base = json!({"user": {"name": "Ada", "email": "ada@example.com"}});
        let patch = json!({"user": {"email": null, "name": "Grace"}});
        merge_non_null(&mut base, &patch);
        assert_eq!(base, json!({"user": {"name": "Grace", "email": "ada@example.com"}}));
    }

    #[test]
    fn replaces_arrays_wholesale() {
        let mut base = json!({"roles": ["reader"]});
        let patch = json!({"roles": ["writer", "admin"]});
        merge_non_null(&mut base, &patch);
        assert_eq!(base, json!({"roles": ["writer", "admin"]}));
    }
}
"#,
                setup_code: None,
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
    ]
}
