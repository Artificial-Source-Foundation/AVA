//! Board of Directors — multi-model consensus for complex tasks.
//!
//! The Board convenes 3 (configurable) SOTA models, each with a distinct
//! analytical personality (Analytical, Pragmatic, Creative).  They receive the
//! goal plus scout reports, produce independent opinions in parallel, and then
//! a consensus is synthesised from the collected votes.
//!
//! # Usage
//! ```ignore
//! let board = Board::new(vec![
//!     BoardMember::new("Opus", opus_provider, BoardPersonality::Analytical),
//!     BoardMember::new("Gemini", gemini_provider, BoardPersonality::Pragmatic),
//!     BoardMember::new("GPT", gpt_provider, BoardPersonality::Creative),
//! ]);
//! let result = board.convene("refactor auth", &scout_reports).await?;
//! println!("{}", result.consensus);
//! ```

use std::sync::Arc;

use ava_llm::provider::LLMProvider;
use ava_types::Result;
use futures::future::join_all;
use serde::{Deserialize, Serialize};

use crate::scout::ScoutReport;

// ---------------------------------------------------------------------------
// Personality prompts
// ---------------------------------------------------------------------------

const ANALYTICAL_PROMPT: &str = "\
You are a rigorous engineer on a Board of Directors reviewing a technical proposal. \
Focus on correctness, edge cases, and potential bugs. Be skeptical of shortcuts. \
Identify risks that others might overlook. Prioritize reliability and safety.";

const PRAGMATIC_PROMPT: &str = "\
You are a practical engineer on a Board of Directors reviewing a technical proposal. \
Focus on the simplest solution that works. Ship fast, iterate later. \
Avoid over-engineering. Prioritize delivering value quickly with minimal complexity.";

const CREATIVE_PROMPT: &str = "\
You are an innovative architect on a Board of Directors reviewing a technical proposal. \
Consider novel approaches and long-term maintainability. Think about extensibility, \
patterns, and how the solution fits into the broader system. Prioritize elegance and sustainability.";

const BOARD_INSTRUCTIONS: &str = "\
Given the goal and codebase analysis below, provide your recommendation.

Respond in the following structure:

## Recommendation
A clear, actionable recommendation (2-4 sentences).

## Approach
Your suggested approach — concrete steps to accomplish the goal.

## Risks
- List specific risks or concerns (one per bullet).

## Vote
One of:
- **Approve** — the proposed approach works as-is
- **Alternative** — suggest a different approach (describe it above)
- **NeedsMoreInfo** — more investigation is required before proceeding";

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/// A distinct analytical personality assigned to each board member.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum BoardPersonality {
    /// Focuses on correctness, edge cases, and potential bugs.
    Analytical,
    /// Focuses on simplicity and shipping fast.
    Pragmatic,
    /// Focuses on novel approaches and long-term maintainability.
    Creative,
}

impl BoardPersonality {
    /// Return the personality-specific system prompt fragment.
    pub fn prompt(self) -> &'static str {
        match self {
            Self::Analytical => ANALYTICAL_PROMPT,
            Self::Pragmatic => PRAGMATIC_PROMPT,
            Self::Creative => CREATIVE_PROMPT,
        }
    }
}

impl std::fmt::Display for BoardPersonality {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Analytical => write!(f, "Analytical"),
            Self::Pragmatic => write!(f, "Pragmatic"),
            Self::Creative => write!(f, "Creative"),
        }
    }
}

/// A single board member — an LLM with a personality.
pub struct BoardMember {
    /// Display name, e.g. "Opus", "Gemini", "GPT".
    pub name: String,
    /// The LLM provider to use for this member.
    pub provider: Arc<dyn LLMProvider>,
    /// Analytical personality guiding this member's perspective.
    pub personality: BoardPersonality,
}

impl BoardMember {
    pub fn new(
        name: impl Into<String>,
        provider: Arc<dyn LLMProvider>,
        personality: BoardPersonality,
    ) -> Self {
        Self {
            name: name.into(),
            provider,
            personality,
        }
    }
}

/// A single board member's opinion on the proposed approach.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BoardOpinion {
    /// The name of the board member who produced this opinion.
    pub member_name: String,
    /// The member's personality type.
    pub personality: BoardPersonality,
    /// High-level recommendation (2-4 sentences).
    pub recommendation: String,
    /// Concrete approach / steps suggested.
    pub approach: String,
    /// Specific risks or concerns identified.
    pub risks: Vec<String>,
    /// The member's vote on the proposal.
    pub vote: BoardVote,
    /// Raw full response (for debugging / display).
    pub raw_response: String,
}

/// A board member's vote on the proposed approach.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum BoardVote {
    /// Agrees with the proposed approach.
    Approve,
    /// Suggests a different approach.
    Alternative,
    /// Wants more investigation before deciding.
    NeedsMoreInfo,
}

impl std::fmt::Display for BoardVote {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Approve => write!(f, "Approve"),
            Self::Alternative => write!(f, "Alternative"),
            Self::NeedsMoreInfo => write!(f, "NeedsMoreInfo"),
        }
    }
}

/// The synthesised result of a Board of Directors session.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BoardResult {
    /// Individual opinions from each board member.
    pub opinions: Vec<BoardOpinion>,
    /// Synthesised consensus recommendation from all opinions.
    pub consensus: String,
    /// Human-readable vote tally, e.g. "2/3 approve, 1 suggests alternative".
    pub vote_summary: String,
}

/// The Board of Directors — convenes multiple SOTA models for consensus.
pub struct Board {
    pub members: Vec<BoardMember>,
}

impl Board {
    /// Create a new Board with the given members.
    pub fn new(members: Vec<BoardMember>) -> Self {
        Self { members }
    }

    /// Return the display names of all board members.
    pub fn member_names(&self) -> Vec<String> {
        self.members.iter().map(|m| m.name.clone()).collect()
    }

    /// Convene the board: query each member in parallel, collect opinions,
    /// and synthesise a consensus.
    pub async fn convene(&self, goal: &str, scout_reports: &[ScoutReport]) -> Result<BoardResult> {
        if self.members.is_empty() {
            return Ok(BoardResult {
                opinions: vec![],
                consensus: "No board members configured.".to_string(),
                vote_summary: "0/0".to_string(),
            });
        }

        // Build the shared context from scout reports
        let context = build_board_context(goal, scout_reports);

        // Query all members in parallel
        let futures = self.members.iter().map(|member| {
            let context = context.clone();
            async move { query_member(member, &context).await }
        });

        let results = join_all(futures).await;

        let mut opinions = Vec::new();
        for result in results {
            match result {
                Ok(opinion) => opinions.push(opinion),
                Err(err) => {
                    tracing::warn!(%err, "Board member query failed");
                }
            }
        }

        let vote_summary = build_vote_summary(&opinions);
        let consensus = synthesise_consensus(&opinions);

        Ok(BoardResult {
            opinions,
            consensus,
            vote_summary,
        })
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Build the shared context string sent to all board members.
fn build_board_context(goal: &str, scout_reports: &[ScoutReport]) -> String {
    let mut ctx = format!("# Goal\n\n{goal}\n\n");

    if !scout_reports.is_empty() {
        ctx.push_str("# Codebase Analysis (Scout Reports)\n\n");
        for report in scout_reports {
            ctx.push_str(&report.as_summary());
            ctx.push_str("\n\n---\n\n");
        }
    }

    ctx
}

/// Query a single board member and parse the response into a [`BoardOpinion`].
async fn query_member(member: &BoardMember, context: &str) -> Result<BoardOpinion> {
    let system_prompt = format!("{}\n\n{}", member.personality.prompt(), BOARD_INSTRUCTIONS,);

    let messages = vec![
        ava_types::Message::new(ava_types::Role::System, system_prompt),
        ava_types::Message::new(
            ava_types::Role::User,
            format!(
                "Review this goal and provide your opinion as described in the instructions.\n\n{}",
                context
            ),
        ),
    ];

    let raw = member.provider.generate(&messages).await.map_err(|e| {
        ava_types::AvaError::ToolError(format!("Board member '{}' failed: {e}", member.name))
    })?;
    let recommendation = extract_section(&raw, "Recommendation");
    let approach = extract_section(&raw, "Approach");
    let risks = extract_bullet_list(&raw, "Risks");
    let vote = parse_vote(&raw);

    Ok(BoardOpinion {
        member_name: member.name.clone(),
        personality: member.personality,
        recommendation,
        approach,
        risks,
        vote,
        raw_response: raw,
    })
}

/// Extract a markdown section's content by heading name.
fn extract_section(text: &str, heading: &str) -> String {
    let mut in_section = false;
    let mut lines = Vec::new();

    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("## ") {
            if trimmed.contains(heading) {
                in_section = true;
                continue;
            } else if in_section {
                // Hit the next section
                break;
            }
        }
        if in_section && !trimmed.is_empty() {
            lines.push(line);
        }
    }

    lines.join("\n").trim().to_string()
}

/// Extract a bulleted list from a markdown section.
fn extract_bullet_list(text: &str, heading: &str) -> Vec<String> {
    let section = extract_section(text, heading);
    section
        .lines()
        .filter_map(|line| {
            let trimmed = line.trim();
            trimmed
                .strip_prefix("- ")
                .or_else(|| trimmed.strip_prefix("* "))
                .map(|stripped| stripped.to_string())
        })
        .collect()
}

/// Parse the vote from the response text.
fn parse_vote(text: &str) -> BoardVote {
    let vote_section = extract_section(text, "Vote");
    let lower = vote_section.to_lowercase();

    if lower.contains("approve") {
        BoardVote::Approve
    } else if lower.contains("alternative") {
        BoardVote::Alternative
    } else if lower.contains("needsmoreinfo") || lower.contains("needs more info") {
        BoardVote::NeedsMoreInfo
    } else {
        // Default to Approve if we can't parse
        BoardVote::Approve
    }
}

/// Build a human-readable vote summary.
fn build_vote_summary(opinions: &[BoardOpinion]) -> String {
    let total = opinions.len();
    let approvals = opinions
        .iter()
        .filter(|o| o.vote == BoardVote::Approve)
        .count();
    let alternatives = opinions
        .iter()
        .filter(|o| o.vote == BoardVote::Alternative)
        .count();
    let needs_info = opinions
        .iter()
        .filter(|o| o.vote == BoardVote::NeedsMoreInfo)
        .count();

    let mut parts = Vec::new();
    if approvals > 0 {
        parts.push(format!("{approvals} approve"));
    }
    if alternatives > 0 {
        parts.push(format!("{alternatives} suggest alternative"));
    }
    if needs_info > 0 {
        parts.push(format!("{needs_info} need more info"));
    }

    if parts.is_empty() {
        format!("0/{total}")
    } else {
        format!(
            "{}/{total}: {}",
            approvals + alternatives + needs_info,
            parts.join(", ")
        )
    }
}

/// Synthesise a consensus from the collected opinions.
fn synthesise_consensus(opinions: &[BoardOpinion]) -> String {
    if opinions.is_empty() {
        return "No opinions collected.".to_string();
    }

    let total = opinions.len();
    let approvals = opinions
        .iter()
        .filter(|o| o.vote == BoardVote::Approve)
        .count();
    let alternatives = opinions
        .iter()
        .filter(|o| o.vote == BoardVote::Alternative)
        .count();
    let needs_info = opinions
        .iter()
        .filter(|o| o.vote == BoardVote::NeedsMoreInfo)
        .count();

    let mut consensus = String::new();

    // Vote result
    if approvals == total {
        consensus.push_str("Unanimous approval. ");
    } else if approvals > total / 2 {
        consensus.push_str(&format!("Majority approval ({approvals}/{total}). "));
    } else if alternatives > total / 2 {
        consensus.push_str(&format!(
            "Majority suggests alternative approach ({alternatives}/{total}). "
        ));
    } else if needs_info > total / 2 {
        consensus.push_str(&format!(
            "Majority requests more information ({needs_info}/{total}). "
        ));
    } else {
        consensus.push_str("Mixed opinions — no clear majority. ");
    }

    // Combine recommendations
    consensus.push_str("\n\nKey recommendations:\n");
    for opinion in opinions {
        if !opinion.recommendation.is_empty() {
            consensus.push_str(&format!(
                "- **{} ({})**: {}\n",
                opinion.member_name, opinion.personality, opinion.recommendation
            ));
        }
    }

    // Aggregate unique risks
    let mut all_risks: Vec<&str> = Vec::new();
    for opinion in opinions {
        for risk in &opinion.risks {
            let risk_str = risk.as_str();
            if !all_risks.iter().any(|r| r == &risk_str) {
                all_risks.push(risk_str);
            }
        }
    }
    if !all_risks.is_empty() {
        consensus.push_str("\nAggregated risks:\n");
        for risk in &all_risks {
            consensus.push_str(&format!("- {risk}\n"));
        }
    }

    consensus
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn board_personality_display() {
        assert_eq!(BoardPersonality::Analytical.to_string(), "Analytical");
        assert_eq!(BoardPersonality::Pragmatic.to_string(), "Pragmatic");
        assert_eq!(BoardPersonality::Creative.to_string(), "Creative");
    }

    #[test]
    fn board_personality_prompts_not_empty() {
        assert!(!BoardPersonality::Analytical.prompt().is_empty());
        assert!(!BoardPersonality::Pragmatic.prompt().is_empty());
        assert!(!BoardPersonality::Creative.prompt().is_empty());
    }

    #[test]
    fn board_vote_display() {
        assert_eq!(BoardVote::Approve.to_string(), "Approve");
        assert_eq!(BoardVote::Alternative.to_string(), "Alternative");
        assert_eq!(BoardVote::NeedsMoreInfo.to_string(), "NeedsMoreInfo");
    }

    #[test]
    fn parse_vote_from_text() {
        assert_eq!(parse_vote("## Vote\n**Approve**"), BoardVote::Approve);
        assert_eq!(
            parse_vote("## Vote\n**Alternative** — use a different pattern"),
            BoardVote::Alternative
        );
        assert_eq!(
            parse_vote("## Vote\n**NeedsMoreInfo** — need to see auth module"),
            BoardVote::NeedsMoreInfo
        );
        assert_eq!(
            parse_vote("## Vote\nNeeds more info before deciding"),
            BoardVote::NeedsMoreInfo
        );
    }

    #[test]
    fn extract_section_finds_content() {
        let text = "\
## Recommendation
Use the existing auth module.

## Approach
1. Read auth.rs
2. Extend the middleware

## Risks
- May break existing sessions
- Performance impact
";
        assert_eq!(
            extract_section(text, "Recommendation"),
            "Use the existing auth module."
        );
        assert!(extract_section(text, "Approach").contains("Read auth.rs"));
    }

    #[test]
    fn extract_bullet_list_parses_risks() {
        let text = "\
## Risks
- Risk one
- Risk two
- Risk three

## Vote
Approve
";
        let risks = extract_bullet_list(text, "Risks");
        assert_eq!(risks.len(), 3);
        assert_eq!(risks[0], "Risk one");
        assert_eq!(risks[2], "Risk three");
    }

    #[test]
    fn build_vote_summary_formats_correctly() {
        let opinions = vec![
            BoardOpinion {
                member_name: "A".to_string(),
                personality: BoardPersonality::Analytical,
                recommendation: String::new(),
                approach: String::new(),
                risks: vec![],
                vote: BoardVote::Approve,
                raw_response: String::new(),
            },
            BoardOpinion {
                member_name: "B".to_string(),
                personality: BoardPersonality::Pragmatic,
                recommendation: String::new(),
                approach: String::new(),
                risks: vec![],
                vote: BoardVote::Approve,
                raw_response: String::new(),
            },
            BoardOpinion {
                member_name: "C".to_string(),
                personality: BoardPersonality::Creative,
                recommendation: String::new(),
                approach: String::new(),
                risks: vec![],
                vote: BoardVote::Alternative,
                raw_response: String::new(),
            },
        ];

        let summary = build_vote_summary(&opinions);
        assert!(summary.contains("2 approve"));
        assert!(summary.contains("1 suggest alternative"));
        assert!(summary.contains("3/3"));
    }

    #[test]
    fn synthesise_consensus_unanimous() {
        let opinions = vec![
            BoardOpinion {
                member_name: "A".to_string(),
                personality: BoardPersonality::Analytical,
                recommendation: "Go ahead.".to_string(),
                approach: String::new(),
                risks: vec!["Risk 1".to_string()],
                vote: BoardVote::Approve,
                raw_response: String::new(),
            },
            BoardOpinion {
                member_name: "B".to_string(),
                personality: BoardPersonality::Pragmatic,
                recommendation: "Ship it.".to_string(),
                approach: String::new(),
                risks: vec!["Risk 1".to_string(), "Risk 2".to_string()],
                vote: BoardVote::Approve,
                raw_response: String::new(),
            },
        ];

        let consensus = synthesise_consensus(&opinions);
        assert!(consensus.contains("Unanimous approval"));
        assert!(consensus.contains("Go ahead."));
        assert!(consensus.contains("Ship it."));
        assert!(consensus.contains("Risk 1"));
        assert!(consensus.contains("Risk 2"));
    }

    #[test]
    fn synthesise_consensus_empty() {
        assert_eq!(synthesise_consensus(&[]), "No opinions collected.");
    }

    #[test]
    fn build_board_context_includes_goal_and_reports() {
        let reports = vec![ScoutReport {
            id: uuid::Uuid::new_v4(),
            query: "auth module".to_string(),
            findings: "Uses JWT.".to_string(),
            files_examined: vec!["auth.rs".to_string()],
            relevant_code: vec![],
            suggestions: vec![],
        }];

        let ctx = build_board_context("Refactor auth", &reports);
        assert!(ctx.contains("Refactor auth"));
        assert!(ctx.contains("auth module"));
        assert!(ctx.contains("Uses JWT."));
    }

    #[test]
    fn build_board_context_no_reports() {
        let ctx = build_board_context("Simple task", &[]);
        assert!(ctx.contains("Simple task"));
        assert!(!ctx.contains("Scout Reports"));
    }

    #[test]
    fn board_member_names() {
        let board = Board::new(vec![]);
        assert!(board.member_names().is_empty());
    }

    #[tokio::test]
    async fn board_convene_empty_members() {
        let board = Board::new(vec![]);
        let result = board.convene("goal", &[]).await.unwrap();
        assert!(result.opinions.is_empty());
        assert_eq!(result.consensus, "No board members configured.");
    }

    #[test]
    fn board_opinion_serialization_roundtrip() {
        let opinion = BoardOpinion {
            member_name: "Opus".to_string(),
            personality: BoardPersonality::Analytical,
            recommendation: "Use the auth module.".to_string(),
            approach: "Step 1, Step 2.".to_string(),
            risks: vec!["Risk A".to_string()],
            vote: BoardVote::Approve,
            raw_response: "full response".to_string(),
        };

        let json = serde_json::to_string(&opinion).unwrap();
        let restored: BoardOpinion = serde_json::from_str(&json).unwrap();
        assert_eq!(restored.member_name, "Opus");
        assert_eq!(restored.vote, BoardVote::Approve);
        assert_eq!(restored.personality, BoardPersonality::Analytical);
    }

    #[test]
    fn board_result_serialization_roundtrip() {
        let result = BoardResult {
            opinions: vec![],
            consensus: "All good.".to_string(),
            vote_summary: "0/0".to_string(),
        };

        let json = serde_json::to_string(&result).unwrap();
        let restored: BoardResult = serde_json::from_str(&json).unwrap();
        assert_eq!(restored.consensus, "All good.");
    }
}
