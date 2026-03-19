//! System prompts for Praxis multi-agent roles.
//!
//! Each tier (Director, Lead, Worker) gets a purpose-built system prompt
//! that defines its responsibilities, communication style, and boundaries.

use crate::Domain;

/// Returns a human-readable label for a domain (e.g. "Backend", "QA").
fn domain_label(domain: &Domain) -> &'static str {
    match domain {
        Domain::Frontend => "Frontend",
        Domain::Backend => "Backend",
        Domain::QA => "QA",
        Domain::Research => "Research",
        Domain::Debug => "Debug",
        Domain::Fullstack => "Fullstack",
        Domain::DevOps => "DevOps",
    }
}

/// System prompt for the Director agent.
///
/// The Director orchestrates leads and workers to accomplish the user's goal.
/// Kept concise (~200 words) — dynamic context (scout reports, plan state) provides details.
pub fn director_system_prompt() -> String {
    String::from(
        "You are an autonomous senior engineer leading a development team. \
        Gather context, plan, delegate, and verify without waiting for additional prompts.\n\
        \n\
        For simple tasks (single-file fixes), work directly. For tasks requiring parallel work across \
        domains, spawn leads. Most tasks need 1-3 leads, not more.\n\
        \n\
        Workflow: send scouts to read the codebase, form a plan, assign leads with specific files \
        (no overlapping assignments), and set execution order — parallel when tasks are independent, \
        sequential when they depend on each other. Each lead gets its own git worktree.\n\
        \n\
        Report state changes concisely: \"Pedro finished the JWT middleware\" is good. \
        Turn-by-turn narration is not. Relay lead questions to the user clearly and wait for an answer.\n\
        \n\
        Before declaring work complete, verify the changes compile and tests pass. \
        If a worker fails, let the lead handle it first — only escalate to the user when the lead cannot resolve it.\n\
        \n\
        Only make changes the user requested. Do not add features, refactor code, or improve things beyond the stated goal.",
    )
}

/// System prompt for a Lead agent.
///
/// Leads manage workers within their domain, splitting tasks into
/// worker-sized subtasks and coordinating file access. Kept concise (~150 words).
pub fn lead_system_prompt(domain: &str) -> String {
    format!(
        "You are the {domain} Lead, a domain specialist managing junior workers.\n\
        \n\
        Split your assigned task into worker-sized subtasks. Assign specific files to each worker — \
        no overlapping file assignments. Run workers in parallel when their tasks are independent, \
        sequentially when one depends on another.\n\
        \n\
        Your workers share your git worktree, so coordinate file access carefully.\n\
        \n\
        Review each worker's output before reporting to the Director. Fix small issues yourself; \
        spawn a fix worker for larger ones. Report results, questions, or ideas — not play-by-play.\n\
        \n\
        Before reporting completion, verify that workers' changes compile and tests pass. \
        Only do what was assigned to you — do not expand scope or refactor beyond the task.",
    )
}

/// System prompt for a Worker agent.
///
/// Workers are focused executors that handle a specific subtask
/// assigned by their Lead. Kept concise (~100 words).
pub fn worker_system_prompt(name: &str, domain: &str) -> String {
    format!(
        "You are {name}, a junior developer on the {domain} team. Complete the specific task your lead assigned.\n\
        \n\
        Read file contents before editing — do not speculate about code you have not opened. \
        Only modify your assigned files. If you intend to call multiple tools and there are no \
        dependencies between them, make all independent calls in parallel.\n\
        \n\
        When finished, report what you changed and any issues you found. \
        If you are unsure about something, ask your lead, not the user. \
        Before reporting completion, verify your changes compile.",
    )
}

/// Convenience: generate a lead system prompt from a [`Domain`].
pub fn lead_system_prompt_for_domain(domain: &Domain) -> String {
    lead_system_prompt(domain_label(domain))
}

/// Convenience: generate a worker system prompt from a name and [`Domain`].
pub fn worker_system_prompt_for_domain(name: &str, domain: &Domain) -> String {
    worker_system_prompt(name, domain_label(domain))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn director_prompt_not_empty() {
        let prompt = director_system_prompt();
        assert!(!prompt.is_empty());
        assert!(prompt.contains("senior engineer"));
        assert!(prompt.contains("delegate"));
    }

    #[test]
    fn lead_prompt_includes_domain() {
        let prompt = lead_system_prompt("Backend");
        assert!(prompt.contains("Backend Lead"));
        assert!(prompt.contains("worker-sized subtasks"));
    }

    #[test]
    fn lead_prompt_for_domain_variant() {
        let prompt = lead_system_prompt_for_domain(&Domain::QA);
        assert!(prompt.contains("QA Lead"));
    }

    #[test]
    fn worker_prompt_includes_name_and_domain() {
        let prompt = worker_system_prompt("Pedro", "Backend");
        assert!(prompt.contains("Pedro"));
        assert!(prompt.contains("Backend team"));
        assert!(prompt.contains("assigned"));
    }

    #[test]
    fn worker_prompt_for_domain_variant() {
        let prompt = worker_system_prompt_for_domain("Sofia", &Domain::Frontend);
        assert!(prompt.contains("Sofia"));
        assert!(prompt.contains("Frontend team"));
    }

    #[test]
    fn all_domains_have_labels() {
        let domains = [
            Domain::Frontend,
            Domain::Backend,
            Domain::QA,
            Domain::Research,
            Domain::Debug,
            Domain::Fullstack,
            Domain::DevOps,
        ];
        for domain in &domains {
            let label = domain_label(domain);
            assert!(!label.is_empty());
            // Lead prompt should work for every domain
            let prompt = lead_system_prompt_for_domain(domain);
            assert!(prompt.contains(label));
        }
    }
}
