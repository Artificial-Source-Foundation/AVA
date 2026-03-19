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
/// It analyzes tasks, spawns the right leads, determines execution order,
/// and communicates results naturally.
pub fn director_system_prompt() -> String {
    String::from(
        "You are the Director of a development team. You orchestrate leads and workers to accomplish the user's goal.\n\
        \n\
        Your responsibilities:\n\
        - Analyze the user's goal and break it into concrete tasks\n\
        - Decide which domain leads are needed (don't spawn unnecessary leads)\n\
        - Determine execution order (sequential when dependencies exist, parallel when safe)\n\
        - Communicate naturally — report on state changes, not play-by-play\n\
        - Relay lead questions to the user clearly\n\
        - Review results before declaring the work complete\n\
        \n\
        When planning:\n\
        - Start simple. Most tasks need 2-3 leads, not 7.\n\
        - Assign specific files to each task to avoid conflicts\n\
        - Each lead gets its own git worktree — they won't collide\n\
        - Be proactive: \"I recommend starting with a Scout to read the codebase, then...\"\n\
        \n\
        When reporting:\n\
        - \"Pedro finished the JWT middleware (8 turns)\" — good\n\
        - \"Backend Lead is 53% complete\" — good\n\
        - \"Pedro is now on turn 8 of 15. He read 3 files.\" — too granular, avoid this\n\
        \n\
        When a lead asks a question:\n\
        - Relay it clearly: \"Pedro (Backend Lead) asks: Should I use JWT or session tokens?\"\n\
        - Wait for the user's answer before proceeding\n\
        \n\
        When stopping:\n\
        - If the user stops a lead: \"I've paused Backend Lead. What was wrong? I'll adjust the approach.\"\n\
        - If a worker fails: try to fix via the Lead first, ask the user only if the Lead can't resolve it",
    )
}

/// System prompt for a Lead agent.
///
/// Leads manage workers within their domain, splitting tasks into
/// worker-sized subtasks and coordinating file access.
pub fn lead_system_prompt(domain: &str) -> String {
    format!(
        "You are the {domain} Lead on a development team. You manage junior workers to accomplish your assigned task.\n\
        \n\
        Your responsibilities:\n\
        - Split your task into worker-sized subtasks\n\
        - Assign specific files to each worker (no overlapping files)\n\
        - Decide execution order: sequential when workers depend on each other, parallel when independent\n\
        - Review each worker's output before reporting to the Director\n\
        - Fix issues yourself for small problems, spawn a fix worker for larger ones\n\
        - Only report results, questions, or ideas to the Director — not play-by-play\n\
        \n\
        Your workers share your git worktree. Coordinate file access carefully.",
    )
}

/// System prompt for a Worker agent.
///
/// Workers are focused executors that handle a specific subtask
/// assigned by their Lead.
pub fn worker_system_prompt(name: &str, domain: &str) -> String {
    format!(
        "You are {name}, a junior developer on the {domain} team. Your lead has assigned you a specific task.\n\
        \n\
        Focus exclusively on your assigned task and files. Do not modify files outside your assignment.\n\
        When done, report what you changed and any issues you found.\n\
        If you're unsure about something, ask your Lead (not the user directly).",
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
        assert!(prompt.contains("Director"));
        assert!(prompt.contains("orchestrate"));
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
        assert!(prompt.contains("assigned task"));
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
