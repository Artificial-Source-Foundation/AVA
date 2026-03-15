//! Discovery and loading of project instruction files for system prompt injection.
//!
//! Checks multiple well-known paths for instruction files and concatenates them
//! into a single string suitable for inclusion in the agent's system prompt.

use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

/// Well-known instruction file names checked in the project root.
const PROJECT_ROOT_FILES: &[&str] = &[
    "AGENTS.md",
    "CLAUDE.md",
    ".cursorrules",
    ".github/copilot-instructions.md",
];

/// Supported skill directories for zero-config interoperability.
/// Order defines precedence within each scope (global/project).
const SKILL_DIRS: &[&str] = &[".claude/skills", ".agents/skills", ".ava/skills"];

/// Estimate token count (rough: ~4 chars per token).
fn estimate_tokens(text: &str) -> usize {
    text.len() / 4
}

/// Trim instructions to fit within a token budget.
///
/// When the model's context window is small (e.g. 128K), the raw project
/// instructions (CLAUDE.md + AGENTS.md + rules) can consume most of the
/// available context. This function truncates at a section boundary so the
/// agent still has room for conversation and tool output.
pub fn trim_instructions_to_budget(instructions: &str, max_tokens: usize) -> String {
    let estimated = estimate_tokens(instructions);
    if estimated <= max_tokens {
        return instructions.to_string();
    }

    let original_k = estimated / 1000;
    let budget_k = max_tokens / 1000;

    // Truncate to fit the character budget
    let max_chars = max_tokens * 4;
    let trimmed = &instructions[..max_chars.min(instructions.len())];

    // Find the last complete section (## heading) boundary to avoid mid-sentence cuts
    let result = if let Some(last_heading) = trimmed.rfind("\n## ") {
        format!(
            "{}\n\n[... instructions trimmed to fit context window ...]",
            &trimmed[..last_heading]
        )
    } else {
        format!("{}\n\n[... instructions trimmed ...]", trimmed)
    };

    tracing::warn!(
        original_tokens_k = original_k,
        budget_tokens_k = budget_k,
        "Instructions trimmed from ~{}K to ~{}K tokens to fit context window",
        original_k,
        budget_k
    );

    result
}

/// Load project instructions from the current working directory and global config.
/// Returns `None` if no instruction files are found.
/// Each file's content is prefixed with `# From: <filepath>` header.
pub fn load_project_instructions() -> Option<String> {
    load_project_instructions_with_config(&[])
}

/// Load project instructions with additional user-configured paths.
///
/// `extra_paths` are file paths or glob patterns relative to the project root.
/// They are loaded after the standard instruction files.
pub fn load_project_instructions_with_config(extra_paths: &[String]) -> Option<String> {
    let cwd = std::env::current_dir().ok()?;
    let home = dirs::home_dir();
    load_from_root_with_extras(&cwd, home.as_deref(), extra_paths)
}

/// Internal implementation that accepts explicit root and home paths for testability.
#[cfg(test)]
fn load_from_root(root: &Path, home: Option<&Path>) -> Option<String> {
    load_from_root_with_extras(root, home, &[])
}

/// Internal implementation that accepts explicit root, home, and extra instruction paths.
fn load_from_root_with_extras(
    root: &Path,
    home: Option<&Path>,
    extra_paths: &[String],
) -> Option<String> {
    let mut seen = HashSet::new();
    let mut sections = Vec::new();

    // 1. Global user-level instructions: ~/.ava/AGENTS.md
    if let Some(home) = home {
        let global = home.join(".ava").join("AGENTS.md");
        try_load_file(&global, &mut seen, &mut sections);
    }

    // 1b. Walk parent directories for AGENTS.md (monorepo support)
    // Stop at filesystem root or directory containing .git
    {
        let mut ancestors = Vec::new();
        let mut dir = root.parent();
        while let Some(d) = dir {
            if d.join(".git").exists() {
                // This directory is a repo boundary — include it but don't go higher
                ancestors.push(d.to_path_buf());
                break;
            }
            ancestors.push(d.to_path_buf());
            dir = d.parent();
        }
        // Load in top-down order (outermost first) so more-specific rules take priority
        ancestors.reverse();
        for ancestor in &ancestors {
            for name in &["AGENTS.md", "CLAUDE.md"] {
                let path = ancestor.join(name);
                try_load_file(&path, &mut seen, &mut sections);
            }
        }
    }

    // 2. Project root files
    for name in PROJECT_ROOT_FILES {
        let path = root.join(name);
        try_load_file(&path, &mut seen, &mut sections);
    }

    // 2b. Project-level .ava/AGENTS.md (inside .ava dir)
    let project_ava_agents = root.join(".ava").join("AGENTS.md");
    try_load_file(&project_ava_agents, &mut seen, &mut sections);

    // 3. .ava/rules/*.md — sorted alphabetically
    let rules_dir = root.join(".ava").join("rules");
    if rules_dir.is_dir() {
        if let Ok(entries) = fs::read_dir(&rules_dir) {
            let mut rule_files: Vec<PathBuf> = entries
                .filter_map(|e| e.ok())
                .map(|e| e.path())
                .filter(|p| {
                    p.extension()
                        .map(|ext| ext.eq_ignore_ascii_case("md"))
                        .unwrap_or(false)
                })
                .collect();
            rule_files.sort();
            for path in rule_files {
                try_load_rule_file(&path, root, &mut seen, &mut sections);
            }
        }
    }

    // 4. User-configured extra instruction paths (from config.yaml `instructions:`)
    for extra in extra_paths {
        if extra.contains('*') {
            // Treat as glob pattern relative to root
            let full_pattern = root.join(extra);
            if let Ok(paths) = glob::glob(&full_pattern.to_string_lossy()) {
                let mut matched: Vec<PathBuf> = paths.filter_map(|p| p.ok()).collect();
                matched.sort();
                for path in matched {
                    try_load_file(&path, &mut seen, &mut sections);
                }
            }
        } else {
            let path = root.join(extra);
            try_load_file(&path, &mut seen, &mut sections);
        }
    }

    // 5. Skill discovery (global first, then project).
    // Supported roots: .claude/skills, .agents/skills, .ava/skills
    load_skill_sections(root, home, &mut seen, &mut sections);

    if sections.is_empty() {
        return None;
    }

    let content = sections.join("\n\n");
    Some(format!(
        "# Project Instructions\n\nFollow the instructions below for this project.\n\n{}",
        content
    ))
}

/// Load discovered skill files as additional instruction sections.
/// Skill files are discovered from both global and project scopes.
fn load_skill_sections(
    root: &Path,
    home: Option<&Path>,
    seen: &mut HashSet<PathBuf>,
    sections: &mut Vec<String>,
) {
    if let Some(home) = home {
        for skill_dir in SKILL_DIRS {
            collect_skill_files(&home.join(skill_dir), sections, seen);
        }
    }

    for skill_dir in SKILL_DIRS {
        collect_skill_files(&root.join(skill_dir), sections, seen);
    }
}

/// Collect SKILL.md files from a skill directory.
/// Supported layout:
/// - <dir>/SKILL.md
/// - <dir>/<skill-name>/SKILL.md
fn collect_skill_files(skill_dir: &Path, sections: &mut Vec<String>, seen: &mut HashSet<PathBuf>) {
    if !skill_dir.is_dir() {
        return;
    }

    let direct_skill = skill_dir.join("SKILL.md");
    try_load_skill_file(&direct_skill, seen, sections);

    if let Ok(entries) = fs::read_dir(skill_dir) {
        let mut nested_skill_files: Vec<PathBuf> = entries
            .filter_map(|entry| entry.ok())
            .map(|entry| entry.path())
            .filter(|path| path.is_dir())
            .map(|dir| dir.join("SKILL.md"))
            .filter(|path| path.is_file())
            .collect();
        nested_skill_files.sort();

        for skill_file in nested_skill_files {
            try_load_skill_file(&skill_file, seen, sections);
        }
    }
}

/// Try to load a skill file and append it as a section.
/// Deduplicates by canonical path and strips optional YAML frontmatter.
fn try_load_skill_file(path: &Path, seen: &mut HashSet<PathBuf>, sections: &mut Vec<String>) {
    let Ok(canonical) = fs::canonicalize(path) else {
        return;
    };

    if !seen.insert(canonical) {
        tracing::debug!("skipping duplicate instruction file: {}", path.display());
        return;
    }

    let Ok(content) = fs::read_to_string(path) else {
        return;
    };

    let trimmed = content.trim();
    if trimmed.is_empty() {
        tracing::debug!("skipping empty instruction file: {}", path.display());
        return;
    }

    let (_, body) = parse_frontmatter(trimmed);
    let body = body.trim();
    if body.is_empty() {
        tracing::debug!(
            "skipping empty instruction file (after frontmatter): {}",
            path.display()
        );
        return;
    }

    tracing::debug!("loaded instruction file: {}", path.display());
    sections.push(format!("# From: {}\n\n{}", path.display(), body));
}

/// Parse optional YAML frontmatter from markdown content.
/// Returns (optional glob patterns, content after frontmatter).
fn parse_frontmatter(content: &str) -> (Option<Vec<String>>, &str) {
    if !content.starts_with("---\n") {
        return (None, content);
    }

    // Find closing ---
    let after_opening = &content[4..]; // skip "---\n"
    let Some(closing) = after_opening.find("\n---\n") else {
        return (None, content); // no closing delimiter
    };

    let frontmatter = &after_opening[..closing];
    let body = &after_opening[closing + 5..]; // skip "\n---\n"

    // Simple parsing: look for `paths:` followed by `  - "pattern"` lines
    let mut in_paths = false;
    let mut patterns = Vec::new();

    for line in frontmatter.lines() {
        let trimmed = line.trim();
        if trimmed == "paths:" {
            in_paths = true;
            continue;
        }
        if in_paths {
            // Lines like `  - "**/*.py"` or `  - "scripts/**"`
            if let Some(rest) = trimmed.strip_prefix("- ") {
                let pattern = rest.trim().trim_matches('"').trim_matches('\'');
                if !pattern.is_empty() {
                    patterns.push(pattern.to_string());
                }
            } else if !trimmed.is_empty() {
                // Non-list line means we've left the paths section
                in_paths = false;
            }
        }
    }

    if patterns.is_empty() {
        (None, body)
    } else {
        (Some(patterns), body)
    }
}

/// Check if any of the glob patterns match at least one file under `root`.
fn has_matching_files(root: &Path, patterns: &[String]) -> bool {
    for pattern in patterns {
        let full_pattern = root.join(pattern);
        if let Ok(mut paths) = glob::glob(&full_pattern.to_string_lossy()) {
            if paths.next().is_some() {
                return true;
            }
        }
    }
    false
}

/// Try to load a rule file, respecting optional frontmatter path globs.
/// If the file has `paths:` frontmatter, only include it if matching files exist.
fn try_load_rule_file(
    path: &Path,
    root: &Path,
    seen: &mut HashSet<PathBuf>,
    sections: &mut Vec<String>,
) {
    let Ok(canonical) = fs::canonicalize(path) else {
        return;
    };

    if !seen.insert(canonical) {
        tracing::debug!("skipping duplicate instruction file: {}", path.display());
        return;
    }

    let Ok(content) = fs::read_to_string(path) else {
        return;
    };

    let trimmed = content.trim();
    if trimmed.is_empty() {
        tracing::debug!("skipping empty instruction file: {}", path.display());
        return;
    }

    let (paths, body) = parse_frontmatter(trimmed);

    if let Some(ref patterns) = paths {
        if !has_matching_files(root, patterns) {
            tracing::debug!(
                "skipping rule file (no matching files for paths globs): {}",
                path.display()
            );
            return;
        }
    }

    let body = body.trim();
    if body.is_empty() {
        tracing::debug!(
            "skipping empty instruction file (after frontmatter): {}",
            path.display()
        );
        return;
    }

    tracing::debug!("loaded instruction file: {}", path.display());
    sections.push(format!("# From: {}\n\n{}", path.display(), body));
}

/// Load contextual instructions for a specific file path.
///
/// Walks from the file's parent directory up to `project_root`, looking for `AGENTS.md`.
/// Returns the first one found (most specific / closest to the file), or `None`.
/// This is intended to be called when the agent reads a file, so that per-directory
/// instructions can be injected into the tool result context.
pub fn contextual_instructions_for_file(file_path: &Path, project_root: &Path) -> Option<String> {
    let mut dir = file_path.parent()?;
    loop {
        let agents_md = dir.join("AGENTS.md");
        if agents_md.is_file() {
            if let Ok(content) = fs::read_to_string(&agents_md) {
                let trimmed = content.trim();
                if !trimmed.is_empty() {
                    return Some(format!(
                        "# Context from {}\n\n{}",
                        agents_md.display(),
                        trimmed
                    ));
                }
            }
        }
        // Don't go above project root
        if dir == project_root {
            break;
        }
        dir = dir.parent()?;
    }
    None
}

/// Try to read a single file and append it as a section.
/// Deduplicates by canonical path and skips empty content.
fn try_load_file(path: &Path, seen: &mut HashSet<PathBuf>, sections: &mut Vec<String>) {
    // Resolve canonical path for deduplication
    let Ok(canonical) = fs::canonicalize(path) else {
        return; // file doesn't exist or is inaccessible
    };

    if !seen.insert(canonical) {
        tracing::debug!("skipping duplicate instruction file: {}", path.display());
        return;
    }

    let Ok(content) = fs::read_to_string(path) else {
        return;
    };

    let trimmed = content.trim();
    if trimmed.is_empty() {
        tracing::debug!("skipping empty instruction file: {}", path.display());
        return;
    }

    tracing::debug!("loaded instruction file: {}", path.display());
    sections.push(format!("# From: {}\n\n{}", path.display(), trimmed));
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn test_trim_instructions_no_trimming_needed() {
        let instructions = "## Section 1\nSome content.\n\n## Section 2\nMore content.";
        // Budget is larger than the content
        let result = trim_instructions_to_budget(instructions, 100_000);
        assert_eq!(result, instructions);
    }

    #[test]
    fn test_trim_instructions_cuts_at_section_boundary() {
        let section1 = "## Section 1\n".to_string() + &"a".repeat(400);
        let section2 = "\n\n## Section 2\n".to_string() + &"b".repeat(400);
        let instructions = format!("{}{}", section1, section2);
        // Budget allows ~120 tokens = ~480 chars. Section 1 is ~413 chars.
        // After trimming to 480 chars we should find the last ## heading boundary.
        let result = trim_instructions_to_budget(&instructions, 120);
        assert!(result.contains("Section 1"));
        assert!(result.contains("[... instructions trimmed to fit context window ...]"));
        assert!(!result.contains("Section 2"));
    }

    #[test]
    fn test_trim_instructions_no_heading_boundary() {
        let instructions = "a".repeat(2000);
        let result = trim_instructions_to_budget(&instructions, 100);
        // 100 tokens * 4 = 400 chars
        assert!(result.len() < 500);
        assert!(result.contains("[... instructions trimmed ...]"));
    }

    #[test]
    fn test_estimate_tokens() {
        assert_eq!(estimate_tokens("abcd"), 1);
        assert_eq!(estimate_tokens("abcdefgh"), 2);
        assert_eq!(estimate_tokens(""), 0);
    }

    #[test]
    fn test_no_instructions_returns_none() {
        let tmp = TempDir::new().unwrap();
        let result = load_from_root(tmp.path(), None);
        assert!(result.is_none());
    }

    #[test]
    fn test_agents_md_discovered() {
        let tmp = TempDir::new().unwrap();
        fs::write(tmp.path().join("AGENTS.md"), "Be helpful.").unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(text.contains("# Project Instructions"));
        assert!(text.contains("Be helpful."));
        assert!(text.contains("AGENTS.md"));
    }

    #[test]
    fn test_rules_directory() {
        let tmp = TempDir::new().unwrap();
        let rules = tmp.path().join(".ava").join("rules");
        fs::create_dir_all(&rules).unwrap();
        fs::write(rules.join("style.md"), "Use 4 spaces.").unwrap();
        fs::write(rules.join("testing.md"), "Write tests first.").unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(result.is_some());
        let text = result.unwrap();

        // Both files present
        assert!(text.contains("Use 4 spaces."));
        assert!(text.contains("Write tests first."));

        // style.md appears before testing.md (alphabetical)
        let style_pos = text.find("style.md").unwrap();
        let testing_pos = text.find("testing.md").unwrap();
        assert!(
            style_pos < testing_pos,
            "style.md should appear before testing.md"
        );
    }

    #[test]
    fn test_deduplication() {
        let tmp = TempDir::new().unwrap();
        // AGENTS.md exists in project root — if the same canonical path
        // is referenced twice, it should only appear once.
        fs::write(tmp.path().join("AGENTS.md"), "Instructions here.").unwrap();

        // Simulate home also pointing at the same file via symlink
        let fake_home = TempDir::new().unwrap();
        let ava_dir = fake_home.path().join(".ava");
        fs::create_dir_all(&ava_dir).unwrap();

        // Create a symlink from ~/.ava/AGENTS.md -> project/AGENTS.md
        #[cfg(unix)]
        {
            std::os::unix::fs::symlink(tmp.path().join("AGENTS.md"), ava_dir.join("AGENTS.md"))
                .unwrap();

            let result = load_from_root(tmp.path(), Some(fake_home.path()));
            let text = result.unwrap();

            // "Instructions here." should appear exactly once
            let count = text.matches("Instructions here.").count();
            assert_eq!(count, 1, "duplicate content should be deduplicated");
        }
    }

    #[test]
    fn test_empty_files_skipped() {
        let tmp = TempDir::new().unwrap();
        fs::write(tmp.path().join("AGENTS.md"), "").unwrap();
        fs::write(tmp.path().join("CLAUDE.md"), "   \n  \t  ").unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(
            result.is_none(),
            "empty/whitespace-only files should be skipped"
        );
    }

    #[test]
    fn test_ancestor_walking() {
        let tmp = TempDir::new().unwrap();
        let grandparent = tmp.path().join("grandparent");
        let parent = grandparent.join("parent");
        let child = parent.join("child");
        fs::create_dir_all(&child).unwrap();

        fs::write(grandparent.join("AGENTS.md"), "Grandparent rules.").unwrap();

        let result = load_from_root(&child, None);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("Grandparent rules."),
            "Should find AGENTS.md in ancestor directory"
        );
    }

    #[test]
    fn test_ancestor_stops_at_git() {
        let tmp = TempDir::new().unwrap();
        let above_repo = tmp.path().join("above");
        let repo = above_repo.join("repo");
        let child = repo.join("child");
        fs::create_dir_all(&child).unwrap();

        // Put .git at repo level
        fs::create_dir_all(repo.join(".git")).unwrap();

        // Put AGENTS.md above the repo boundary
        fs::write(above_repo.join("AGENTS.md"), "Should NOT appear.").unwrap();
        // Put AGENTS.md at repo level (should appear — it has .git)
        fs::write(repo.join("AGENTS.md"), "Repo rules.").unwrap();

        let result = load_from_root(&child, None);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            !text.contains("Should NOT appear."),
            "Should not load AGENTS.md above .git boundary"
        );
        assert!(
            text.contains("Repo rules."),
            "Should load AGENTS.md at the .git boundary"
        );
    }

    #[test]
    fn test_rules_with_frontmatter_paths_match() {
        let tmp = TempDir::new().unwrap();
        let rules = tmp.path().join(".ava").join("rules");
        fs::create_dir_all(&rules).unwrap();

        // Create a .py file so the glob matches
        fs::write(tmp.path().join("main.py"), "print('hello')").unwrap();

        let rule_content = "---\npaths:\n  - \"**/*.py\"\n---\nAlways use type hints.";
        fs::write(rules.join("python.md"), rule_content).unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("Always use type hints."),
            "Rule with matching paths glob should be loaded"
        );
        assert!(
            !text.contains("---"),
            "Frontmatter should be stripped from output"
        );
    }

    #[test]
    fn test_rules_with_frontmatter_paths_no_match() {
        let tmp = TempDir::new().unwrap();
        let rules = tmp.path().join(".ava").join("rules");
        fs::create_dir_all(&rules).unwrap();

        // No .go files in the project
        let rule_content = "---\npaths:\n  - \"**/*.go\"\n---\nUse gofmt.";
        fs::write(rules.join("golang.md"), rule_content).unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(
            result.is_none(),
            "Rule with non-matching paths glob should be skipped"
        );
    }

    #[test]
    fn test_rules_without_frontmatter_always_load() {
        let tmp = TempDir::new().unwrap();
        let rules = tmp.path().join(".ava").join("rules");
        fs::create_dir_all(&rules).unwrap();

        fs::write(rules.join("general.md"), "Be concise.").unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("Be concise."),
            "Rule without frontmatter should always load"
        );
    }

    #[test]
    fn test_parse_frontmatter_basic() {
        let content = "---\npaths:\n  - \"**/*.py\"\n  - \"scripts/**\"\n---\nBody text.";
        let (paths, body) = parse_frontmatter(content);
        assert_eq!(
            paths,
            Some(vec!["**/*.py".to_string(), "scripts/**".to_string()])
        );
        assert_eq!(body.trim(), "Body text.");
    }

    #[test]
    fn test_parse_frontmatter_none() {
        let content = "Just regular content.";
        let (paths, body) = parse_frontmatter(content);
        assert!(paths.is_none());
        assert_eq!(body, content);
    }

    #[test]
    fn test_global_and_project_files() {
        let tmp = TempDir::new().unwrap();
        let fake_home = TempDir::new().unwrap();
        let ava_dir = fake_home.path().join(".ava");
        fs::create_dir_all(&ava_dir).unwrap();

        fs::write(ava_dir.join("AGENTS.md"), "Global rules.").unwrap();
        fs::write(tmp.path().join("CLAUDE.md"), "Project rules.").unwrap();

        let result = load_from_root(tmp.path(), Some(fake_home.path()));
        let text = result.unwrap();

        assert!(text.contains("Global rules."));
        assert!(text.contains("Project rules."));

        // Global should come before project
        let global_pos = text.find("Global rules.").unwrap();
        let project_pos = text.find("Project rules.").unwrap();
        assert!(global_pos < project_pos);
    }

    #[test]
    fn test_extra_paths_loaded() {
        let tmp = TempDir::new().unwrap();
        let custom_dir = tmp.path().join("team");
        fs::create_dir_all(&custom_dir).unwrap();
        fs::write(
            custom_dir.join("conventions.md"),
            "Use snake_case everywhere.",
        )
        .unwrap();

        let extras = vec!["team/conventions.md".to_string()];
        let result = load_from_root_with_extras(tmp.path(), None, &extras);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("Use snake_case everywhere."),
            "Extra instruction file should be loaded"
        );
        assert!(
            text.contains("conventions.md"),
            "Header should reference the file path"
        );
    }

    #[test]
    fn test_extra_glob_patterns() {
        let tmp = TempDir::new().unwrap();
        let docs_dir = tmp.path().join("docs");
        fs::create_dir_all(&docs_dir).unwrap();
        fs::write(docs_dir.join("rule1.md"), "First rule.").unwrap();
        fs::write(docs_dir.join("rule2.md"), "Second rule.").unwrap();
        fs::write(docs_dir.join("notes.txt"), "Not a markdown file.").unwrap();

        let extras = vec!["docs/*.md".to_string()];
        let result = load_from_root_with_extras(tmp.path(), None, &extras);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("First rule."),
            "rule1.md should be loaded via glob"
        );
        assert!(
            text.contains("Second rule."),
            "rule2.md should be loaded via glob"
        );
        assert!(
            !text.contains("Not a markdown file."),
            "notes.txt should not match *.md glob"
        );
    }

    #[test]
    fn test_extra_paths_missing_file_skipped() {
        let tmp = TempDir::new().unwrap();
        // No files created — the extra path points to a nonexistent file
        let extras = vec!["nonexistent/file.md".to_string()];
        let result = load_from_root_with_extras(tmp.path(), None, &extras);
        assert!(
            result.is_none(),
            "Missing extra files should be silently skipped"
        );
    }

    #[test]
    fn test_extra_paths_deduplicated_with_standard() {
        let tmp = TempDir::new().unwrap();
        // AGENTS.md is a standard file AND referenced as an extra path
        fs::write(tmp.path().join("AGENTS.md"), "Standard rules.").unwrap();

        let extras = vec!["AGENTS.md".to_string()];
        let result = load_from_root_with_extras(tmp.path(), None, &extras);
        assert!(result.is_some());
        let text = result.unwrap();
        let count = text.matches("Standard rules.").count();
        assert_eq!(
            count, 1,
            "Same file referenced as standard and extra should appear only once"
        );
    }

    #[test]
    fn test_skill_discovery_project_dirs() {
        let tmp = TempDir::new().unwrap();

        let rust_skill = tmp.path().join(".ava").join("skills").join("rust");
        let test_skill = tmp.path().join(".claude").join("skills").join("testing");
        fs::create_dir_all(&rust_skill).unwrap();
        fs::create_dir_all(&test_skill).unwrap();

        fs::write(rust_skill.join("SKILL.md"), "Use idiomatic Rust patterns.").unwrap();
        fs::write(
            test_skill.join("SKILL.md"),
            "Prefer integration tests for boundaries.",
        )
        .unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(text.contains("Use idiomatic Rust patterns."));
        assert!(text.contains("Prefer integration tests for boundaries."));
    }

    #[test]
    fn test_skill_discovery_global_then_project_precedence() {
        let tmp = TempDir::new().unwrap();
        let fake_home = TempDir::new().unwrap();

        let global_skill = fake_home
            .path()
            .join(".agents")
            .join("skills")
            .join("global");
        let project_skill = tmp.path().join(".agents").join("skills").join("project");
        fs::create_dir_all(&global_skill).unwrap();
        fs::create_dir_all(&project_skill).unwrap();

        fs::write(global_skill.join("SKILL.md"), "Global skill guidance.").unwrap();
        fs::write(project_skill.join("SKILL.md"), "Project skill guidance.").unwrap();

        let result = load_from_root(tmp.path(), Some(fake_home.path()));
        assert!(result.is_some());
        let text = result.unwrap();

        let global_pos = text.find("Global skill guidance.").unwrap();
        let project_pos = text.find("Project skill guidance.").unwrap();
        assert!(
            global_pos < project_pos,
            "Global skills should load before project skills"
        );
    }

    #[test]
    fn test_skill_discovery_ignores_non_skill_markdown() {
        let tmp = TempDir::new().unwrap();
        let skill_dir = tmp.path().join(".ava").join("skills").join("misc");
        fs::create_dir_all(&skill_dir).unwrap();
        fs::write(skill_dir.join("README.md"), "This should not load.").unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(result.is_none());
    }

    #[test]
    fn test_skill_discovery_strips_frontmatter() {
        let tmp = TempDir::new().unwrap();
        let skill_dir = tmp.path().join(".claude").join("skills").join("react");
        fs::create_dir_all(&skill_dir).unwrap();
        fs::write(
            skill_dir.join("SKILL.md"),
            "---\nname: React Patterns\ndescription: React guidance\n---\nUse composition-first patterns.",
        )
        .unwrap();

        let result = load_from_root(tmp.path(), None);
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(text.contains("Use composition-first patterns."));
        assert!(!text.contains("name: React Patterns"));
        assert!(!text.contains("description: React guidance"));
    }

    // --- contextual_instructions_for_file tests ---

    #[test]
    fn test_contextual_instructions_in_subdir() {
        let tmp = TempDir::new().unwrap();
        let api_dir = tmp.path().join("src").join("api");
        fs::create_dir_all(&api_dir).unwrap();
        fs::write(api_dir.join("AGENTS.md"), "Use REST conventions.").unwrap();
        fs::write(api_dir.join("handler.rs"), "fn handle() {}").unwrap();

        let result = contextual_instructions_for_file(&api_dir.join("handler.rs"), tmp.path());
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(text.contains("Use REST conventions."));
        assert!(text.contains("AGENTS.md"));
    }

    #[test]
    fn test_contextual_instructions_walks_up() {
        let tmp = TempDir::new().unwrap();
        let src_dir = tmp.path().join("src");
        let api_dir = src_dir.join("api");
        fs::create_dir_all(&api_dir).unwrap();
        // AGENTS.md only in src/, not in src/api/
        fs::write(src_dir.join("AGENTS.md"), "Source-level rules.").unwrap();
        fs::write(api_dir.join("handler.rs"), "fn handle() {}").unwrap();

        let result = contextual_instructions_for_file(&api_dir.join("handler.rs"), tmp.path());
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("Source-level rules."),
            "Should find AGENTS.md in parent directory"
        );
    }

    #[test]
    fn test_contextual_instructions_stops_at_root() {
        let tmp = TempDir::new().unwrap();
        let project_root = tmp.path().join("project");
        let subdir = project_root.join("src");
        fs::create_dir_all(&subdir).unwrap();
        // Put AGENTS.md above the project root — should NOT be found
        fs::write(tmp.path().join("AGENTS.md"), "Should NOT appear.").unwrap();
        fs::write(subdir.join("main.rs"), "fn main() {}").unwrap();

        let result = contextual_instructions_for_file(&subdir.join("main.rs"), &project_root);
        assert!(
            result.is_none(),
            "Should not find AGENTS.md above project root"
        );
    }

    #[test]
    fn test_contextual_instructions_none() {
        let tmp = TempDir::new().unwrap();
        let subdir = tmp.path().join("src");
        fs::create_dir_all(&subdir).unwrap();
        fs::write(subdir.join("main.rs"), "fn main() {}").unwrap();
        // No AGENTS.md anywhere

        let result = contextual_instructions_for_file(&subdir.join("main.rs"), tmp.path());
        assert!(
            result.is_none(),
            "Should return None when no AGENTS.md exists"
        );
    }

    #[test]
    fn test_contextual_instructions_most_specific_wins() {
        let tmp = TempDir::new().unwrap();
        let src_dir = tmp.path().join("src");
        let api_dir = src_dir.join("api");
        fs::create_dir_all(&api_dir).unwrap();
        // AGENTS.md in both src/ and src/api/ — the closer one should win
        fs::write(src_dir.join("AGENTS.md"), "General source rules.").unwrap();
        fs::write(api_dir.join("AGENTS.md"), "API-specific rules.").unwrap();
        fs::write(api_dir.join("handler.rs"), "fn handle() {}").unwrap();

        let result = contextual_instructions_for_file(&api_dir.join("handler.rs"), tmp.path());
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("API-specific rules."),
            "Most specific (closest) AGENTS.md should be returned"
        );
        assert!(
            !text.contains("General source rules."),
            "Parent AGENTS.md should NOT be included when a closer one exists"
        );
    }

    #[test]
    fn test_contextual_instructions_empty_file_skipped() {
        let tmp = TempDir::new().unwrap();
        let subdir = tmp.path().join("src");
        fs::create_dir_all(&subdir).unwrap();
        fs::write(subdir.join("AGENTS.md"), "   \n  \t  ").unwrap();
        // Put a real one at project root
        fs::write(tmp.path().join("AGENTS.md"), "Root instructions.").unwrap();
        fs::write(subdir.join("main.rs"), "fn main() {}").unwrap();

        let result = contextual_instructions_for_file(&subdir.join("main.rs"), tmp.path());
        assert!(result.is_some());
        let text = result.unwrap();
        assert!(
            text.contains("Root instructions."),
            "Should skip empty AGENTS.md and find the root one"
        );
    }
}
