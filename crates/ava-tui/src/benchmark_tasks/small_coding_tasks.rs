use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

/// AVA 3.3.1 small coding suite (Tier 2 compile-and-test).
pub fn small_coding_tasks(_temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    vec![BenchmarkTask {
        name: "small_coding_http_status_class",
        prompt: "Write a Rust function `http_status_class(status: u16) -> &'static str` that returns: `\"informational\"` for 100..=199, `\"success\"` for 200..=299, `\"redirect\"` for 300..=399, `\"client_error\"` for 400..=499, `\"server_error\"` for 500..=599, and `\"invalid\"` otherwise. Only output the Rust code.".to_string(),
        expected_patterns: vec![r"fn\s+http_status_class", r"match|if", r"100|200|500"],
        category: TaskCategory::SmallCoding,
        needs_tools: false,
        test_harness: Some(TestHarness {
            test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn maps_success_range() {
        assert_eq!(http_status_class(204), "success");
    }

    #[test]
    fn maps_client_error_range() {
        assert_eq!(http_status_class(404), "client_error");
    }

    #[test]
    fn maps_server_error_range() {
        assert_eq!(http_status_class(503), "server_error");
    }

    #[test]
    fn maps_invalid_values() {
        assert_eq!(http_status_class(99), "invalid");
        assert_eq!(http_status_class(600), "invalid");
    }
}
"#,
            setup_code: None,
            test_count: 4,
            language: Language::Rust,
        }),
        expected_min_tools: None,
    }]
}
