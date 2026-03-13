use ava_config::RoutingProfile;
use ava_llm::RouteRequirements;
use ava_types::{ImageContent, ThinkingLevel};

#[derive(Debug, Clone)]
pub struct TaskRoutingIntent {
    pub profile: RoutingProfile,
    pub requirements: RouteRequirements,
    pub reasons: Vec<String>,
}

pub fn analyze_task(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> TaskRoutingIntent {
    let trimmed = goal.trim();
    let lower = trimmed.to_lowercase();
    let line_count = trimmed.lines().count();
    let has_images = !images.is_empty();

    let requirements = RouteRequirements {
        needs_vision: has_images,
        prefer_reasoning: thinking != ThinkingLevel::Off,
    };

    let capable_keywords = [
        "debug",
        "investigate",
        "root cause",
        "review",
        "refactor",
        "architecture",
        "migrate",
        "performance",
        "security",
        "failing test",
        "design",
        "implement",
    ];
    let cheap_keywords = [
        "summarize",
        "rewrite",
        "rephrase",
        "explain",
        "list",
        "draft",
        "quick",
        "short",
    ];

    let mut reasons = Vec::new();
    if has_images {
        reasons.push("images attached; keep vision-capable route".to_string());
    }
    if plan_mode {
        reasons.push("plan mode prefers a more capable model".to_string());
    }
    if thinking != ThinkingLevel::Off {
        reasons.push("thinking mode is enabled".to_string());
    }
    if trimmed.len() > 700 || line_count > 8 {
        reasons.push("prompt is long or multi-step".to_string());
    }
    if capable_keywords
        .iter()
        .any(|keyword| contains_keyword(&lower, keyword))
    {
        reasons.push("task wording suggests deeper reasoning/coding work".to_string());
    }
    if !reasons.is_empty() {
        return TaskRoutingIntent {
            profile: RoutingProfile::Capable,
            requirements,
            reasons,
        };
    }

    if trimmed.len() <= 240
        && line_count <= 3
        && cheap_keywords
            .iter()
            .any(|keyword| contains_keyword(&lower, keyword))
    {
        return TaskRoutingIntent {
            profile: RoutingProfile::Cheap,
            requirements,
            reasons: vec!["short low-risk request; prefer cheaper route".to_string()],
        };
    }

    TaskRoutingIntent {
        profile: RoutingProfile::Capable,
        requirements,
        reasons: vec![
            "defaulting to capable route until work looks obviously lightweight".to_string(),
        ],
    }
}

fn contains_keyword(text: &str, keyword: &str) -> bool {
    if keyword.contains(' ') {
        return text.contains(keyword);
    }

    text.split(|ch: char| !ch.is_ascii_alphanumeric())
        .any(|token| !token.is_empty() && token == keyword)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn routing_analysis_prefers_capable_for_debug_work() {
        let intent = analyze_task(
            "Debug the failing provider fallback and explain the root cause.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Capable);
        assert!(intent
            .reasons
            .iter()
            .any(|reason| reason.contains("deeper reasoning")));
    }

    #[test]
    fn routing_analysis_prefers_cheap_for_short_summary_work() {
        let intent = analyze_task(
            "Summarize this diff in two bullets.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Cheap);
    }

    #[test]
    fn routing_analysis_avoids_obvious_substring_false_positive() {
        let intent = analyze_task(
            "Please shortlist the options.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Capable);
        assert!(!intent
            .reasons
            .iter()
            .any(|reason| reason.contains("deeper reasoning")));
    }
}
