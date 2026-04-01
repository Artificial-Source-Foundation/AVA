//! Built-in skill definitions shipped with AVA.
//!
//! These 7 skills cover the most common agent workflows. Each has a prompt
//! template that guides the agent through a structured approach.

use super::skill_def::{SkillContext, SkillDefinition};
use super::skill_registry::SkillRegistry;

/// Register all built-in skills into the given registry.
pub fn register_bundled_skills(registry: &mut SkillRegistry) {
    registry.register(debug_skill());
    registry.register(verify_skill());
    registry.register(loop_skill());
    registry.register(simplify_skill());
    registry.register(stuck_skill());
    registry.register(review_skill());
    registry.register(commit_skill());
}

fn debug_skill() -> SkillDefinition {
    SkillDefinition {
        name: "debug".to_string(),
        description: "Structured debugging: reproduce, isolate, fix, verify".to_string(),
        aliases: vec!["dbg".to_string(), "troubleshoot".to_string()],
        allowed_tools: vec![],
        model: None,
        context: SkillContext::Inline,
        prompt_template: "Follow a structured debugging workflow for: {args}\n\n\
            1. REPRODUCE: First reproduce the issue. Run the failing test or trigger the bug.\n\
            2. ISOLATE: Narrow down the root cause. Add logging, check recent changes, bisect if needed.\n\
            3. FIX: Apply the minimal fix that addresses the root cause, not just symptoms.\n\
            4. VERIFY: Run the original reproduction case plus related tests to confirm the fix.\n\
            Report each step's outcome before moving to the next."
            .to_string(),
    }
}

fn verify_skill() -> SkillDefinition {
    SkillDefinition {
        name: "verify".to_string(),
        description: "Run tests + lint + type check, report issues".to_string(),
        aliases: vec!["check".to_string(), "validate".to_string()],
        allowed_tools: vec![],
        model: None,
        context: SkillContext::Inline,
        prompt_template: "Run a full verification pass on the codebase{args}:\n\n\
            1. Run the test suite and report any failures with file/line details.\n\
            2. Run the linter (clippy, eslint, etc.) and report any warnings or errors.\n\
            3. Run the type checker if applicable and report type errors.\n\
            4. Summarize: how many checks passed, how many failed, and what needs attention.\n\
            If everything passes, confirm the codebase is clean."
            .to_string(),
    }
}

fn loop_skill() -> SkillDefinition {
    SkillDefinition {
        name: "loop".to_string(),
        description: "Iterative refinement until condition passes".to_string(),
        aliases: vec!["iterate".to_string(), "until".to_string()],
        allowed_tools: vec![],
        model: None,
        context: SkillContext::Inline,
        prompt_template: "Iteratively refine until the following condition is met: {args}\n\n\
            On each iteration:\n\
            1. Attempt the task or run the check.\n\
            2. If the condition is not met, analyze what went wrong.\n\
            3. Apply a targeted fix based on the analysis.\n\
            4. Re-run the check.\n\
            Continue until the condition passes or you have attempted 5 iterations. \
            If still failing after 5 attempts, report what you tried and what remains."
            .to_string(),
    }
}

fn simplify_skill() -> SkillDefinition {
    SkillDefinition {
        name: "simplify".to_string(),
        description: "Reduce complexity in selected code".to_string(),
        aliases: vec!["refactor".to_string(), "clean".to_string()],
        allowed_tools: vec![],
        model: None,
        context: SkillContext::Inline,
        prompt_template: "Simplify and reduce complexity in: {args}\n\n\
            Look for: redundant code, overly nested logic, unnecessary abstractions, \
            dead code, and opportunities to use standard library functions. \
            Preserve all existing behavior and public interfaces. \
            Make changes incrementally and verify tests pass after each change."
            .to_string(),
    }
}

fn stuck_skill() -> SkillDefinition {
    SkillDefinition {
        name: "stuck".to_string(),
        description: "Step back, reassess approach, try fresh".to_string(),
        aliases: vec!["unstuck".to_string(), "rethink".to_string()],
        allowed_tools: vec![],
        model: None,
        context: SkillContext::Fork,
        prompt_template: "The previous approach seems stuck. Take a fresh look: {args}\n\n\
            1. Summarize what has been tried so far and why it did not work.\n\
            2. Identify the core constraint or misunderstanding that is blocking progress.\n\
            3. Propose 2-3 alternative approaches, briefly explaining trade-offs.\n\
            4. Pick the most promising alternative and execute it.\n\
            Do not repeat the same approach that already failed."
            .to_string(),
    }
}

fn review_skill() -> SkillDefinition {
    SkillDefinition {
        name: "review".to_string(),
        description: "Code review: security, quality, performance".to_string(),
        aliases: vec!["cr".to_string(), "code-review".to_string()],
        allowed_tools: vec![],
        model: None,
        context: SkillContext::Inline,
        prompt_template: "Perform a thorough code review on: {args}\n\n\
            Check for:\n\
            - **Security**: injection vulnerabilities, unsafe operations, credential exposure\n\
            - **Correctness**: logic errors, edge cases, error handling gaps\n\
            - **Performance**: unnecessary allocations, O(n^2) patterns, missing caching\n\
            - **Maintainability**: unclear names, missing docs, code duplication\n\
            Rate each finding as Critical / Warning / Suggestion with file and line references."
            .to_string(),
    }
}

fn commit_skill() -> SkillDefinition {
    SkillDefinition {
        name: "commit".to_string(),
        description: "Inspect changes, draft message, create commit".to_string(),
        aliases: vec!["ci".to_string()],
        allowed_tools: vec![],
        model: None,
        context: SkillContext::Inline,
        prompt_template: "Prepare a git commit for the current changes{args}:\n\n\
            1. Run `git status` and `git diff` to inspect all staged and unstaged changes.\n\
            2. Analyze the changes and categorize them (feature, fix, refactor, docs, etc.).\n\
            3. Draft a concise commit message following conventional commits style.\n\
            4. Stage the relevant files (avoid committing secrets or generated files).\n\
            5. Create the commit with the drafted message.\n\
            Report the commit hash and summary when done."
            .to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_bundled_skills_registered() {
        let mut registry = SkillRegistry::new();
        register_bundled_skills(&mut registry);
        assert_eq!(registry.len(), 7);
    }

    #[test]
    fn bundled_skill_names() {
        let mut registry = SkillRegistry::new();
        register_bundled_skills(&mut registry);

        let names: Vec<&str> = registry.list().iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"debug"));
        assert!(names.contains(&"verify"));
        assert!(names.contains(&"loop"));
        assert!(names.contains(&"simplify"));
        assert!(names.contains(&"stuck"));
        assert!(names.contains(&"review"));
        assert!(names.contains(&"commit"));
    }

    #[test]
    fn stuck_is_fork_context() {
        let mut registry = SkillRegistry::new();
        register_bundled_skills(&mut registry);

        let stuck = registry.get("stuck").unwrap();
        assert_eq!(stuck.context, SkillContext::Fork);
    }

    #[test]
    fn most_skills_are_inline() {
        let mut registry = SkillRegistry::new();
        register_bundled_skills(&mut registry);

        for skill in registry.list() {
            if skill.name != "stuck" {
                assert_eq!(
                    skill.context,
                    SkillContext::Inline,
                    "{} should be Inline",
                    skill.name
                );
            }
        }
    }

    #[test]
    fn alias_lookup_works() {
        let mut registry = SkillRegistry::new();
        register_bundled_skills(&mut registry);

        assert!(registry.get("dbg").is_some());
        assert_eq!(registry.get("dbg").unwrap().name, "debug");
        assert!(registry.get("cr").is_some());
        assert_eq!(registry.get("cr").unwrap().name, "review");
        assert!(registry.get("ci").is_some());
        assert_eq!(registry.get("ci").unwrap().name, "commit");
    }
}
