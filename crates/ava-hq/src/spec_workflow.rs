use crate::spec::SpecDocument;

pub fn build_spec_goal(spec: &SpecDocument) -> String {
    let remaining_tasks = spec
        .tasks
        .iter()
        .filter(|task| !task.done)
        .map(|task| format!("- {}", task.title))
        .collect::<Vec<_>>();

    let tasks_block = if remaining_tasks.is_empty() {
        "- (none)".to_string()
    } else {
        remaining_tasks.join("\n")
    };

    format!(
        "Spec title: {}\n\nRequirements:\n{}\n\nDesign:\n{}\n\nOpen tasks:\n{}",
        spec.title, spec.requirements, spec.design, tasks_block
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::spec::SpecDocument;

    #[test]
    fn build_spec_goal_includes_only_open_tasks() {
        let mut spec = SpecDocument::new(
            "Auth hardening",
            "Protect session flows",
            "Add stricter token checks",
            vec!["Implement middleware".to_string(), "Add tests".to_string()],
        );
        spec.tasks[1].done = true;

        let goal = build_spec_goal(&spec);
        assert!(goal.contains("Spec title: Auth hardening"));
        assert!(goal.contains("- Implement middleware"));
        assert!(!goal.contains("- Add tests"));
    }
}
