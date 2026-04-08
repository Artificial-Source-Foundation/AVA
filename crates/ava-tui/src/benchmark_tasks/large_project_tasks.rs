use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const LARGE_PROJECT_EVALUATOR_SETUP: &str = r#"use crate::rollout::in_rollout;
use crate::rules::FeatureRule;

pub fn is_feature_enabled(rule: &FeatureRule, user_id: u64, allowlist: &[u64]) -> bool {
    if allowlist.contains(&user_id) {
        return true;
    }

    if !rule.enabled {
        return true;
    }

    in_rollout(user_id, rule.rollout_percent)
}
"#;

pub fn large_project_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let task_dir = temp_dir.join("large_project_feature_flags");

    vec![BenchmarkTask {
        name: "large_project_feature_flags",
        prompt: format!(
            "The directory {dir} contains a small multi-file Rust feature-flag service. \
             Use tools to fix the rollout semantics and disabled-flag behavior without collapsing \
             the project into one file. Keep allowlist override behavior, correct percentage \
             boundaries, and verify the test suite passes.",
            dir = task_dir.display()
        ),
        expected_patterns: vec![r"enabled", r"allowlist", r"rollout|percent"],
        category: TaskCategory::LargeProject,
        needs_tools: true,
        test_harness: Some(TestHarness {
            test_code: r#"
mod rules;
mod rollout;
mod evaluator;

use evaluator::is_feature_enabled;
use rules::FeatureRule;

#[test]
fn disabled_flag_stays_disabled_for_normal_users() {
    let rule = FeatureRule::new("beta_dashboard", false, 100);
    assert!(!is_feature_enabled(&rule, 17, &[]));
}

#[test]
fn allowlist_overrides_disabled_flag() {
    let rule = FeatureRule::new("beta_dashboard", false, 0);
    assert!(is_feature_enabled(&rule, 42, &[42]));
}

#[test]
fn zero_percent_never_rolls_out() {
    let rule = FeatureRule::new("beta_dashboard", true, 0);
    assert!(!is_feature_enabled(&rule, 10, &[]));
}

#[test]
fn hundred_percent_always_rolls_out() {
    let rule = FeatureRule::new("beta_dashboard", true, 100);
    assert!(is_feature_enabled(&rule, 99, &[]));
}

#[test]
fn rollout_uses_strict_upper_bound() {
    let rule = FeatureRule::new("beta_dashboard", true, 25);
    assert!(is_feature_enabled(&rule, 24, &[]));
    assert!(!is_feature_enabled(&rule, 25, &[]));
}
"#,
            setup_code: Some(LARGE_PROJECT_EVALUATOR_SETUP),
            test_count: 5,
            language: Language::Rust,
        }),
        expected_min_tools: Some(6),
    }]
}
