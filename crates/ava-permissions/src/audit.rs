use chrono::{DateTime, Utc};

use crate::tags::{RiskLevel, SafetyTag};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AuditDecision {
    AutoApproved,
    UserApproved,
    UserDenied,
    Blocked,
    SessionApproved,
}

#[derive(Debug, Clone)]
pub struct AuditEntry {
    pub timestamp: DateTime<Utc>,
    pub tool_name: String,
    pub arguments_summary: String,
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
    pub decision: AuditDecision,
}

pub struct AuditLog {
    entries: Vec<AuditEntry>,
    max_entries: usize,
}

impl Default for AuditLog {
    fn default() -> Self {
        Self {
            entries: Vec::new(),
            max_entries: 1000,
        }
    }
}

impl AuditLog {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn record(
        &mut self,
        tool_name: impl Into<String>,
        arguments_summary: impl Into<String>,
        risk_level: RiskLevel,
        tags: Vec<SafetyTag>,
        decision: AuditDecision,
    ) {
        let summary = {
            let s = arguments_summary.into();
            if s.len() > 200 {
                format!("{}...", &s[..197])
            } else {
                s
            }
        };

        self.entries.push(AuditEntry {
            timestamp: Utc::now(),
            tool_name: tool_name.into(),
            arguments_summary: summary,
            risk_level,
            tags,
            decision,
        });

        if self.entries.len() > self.max_entries {
            self.entries.remove(0);
        }
    }

    pub fn recent(&self, n: usize) -> &[AuditEntry] {
        let start = self.entries.len().saturating_sub(n);
        &self.entries[start..]
    }

    pub fn summary(&self) -> AuditSummary {
        let mut auto_approved = 0u32;
        let mut user_approved = 0u32;
        let mut user_denied = 0u32;
        let mut blocked = 0u32;
        let mut session_approved = 0u32;

        for entry in &self.entries {
            match entry.decision {
                AuditDecision::AutoApproved => auto_approved += 1,
                AuditDecision::UserApproved => user_approved += 1,
                AuditDecision::UserDenied => user_denied += 1,
                AuditDecision::Blocked => blocked += 1,
                AuditDecision::SessionApproved => session_approved += 1,
            }
        }

        AuditSummary {
            total: self.entries.len() as u32,
            auto_approved,
            user_approved,
            user_denied,
            blocked,
            session_approved,
        }
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

#[derive(Debug, Clone)]
pub struct AuditSummary {
    pub total: u32,
    pub auto_approved: u32,
    pub user_approved: u32,
    pub user_denied: u32,
    pub blocked: u32,
    pub session_approved: u32,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn record_and_retrieve() {
        let mut log = AuditLog::new();
        log.record(
            "bash",
            "ls -la",
            RiskLevel::Safe,
            vec![],
            AuditDecision::AutoApproved,
        );
        log.record(
            "write",
            "file.rs",
            RiskLevel::Low,
            vec![SafetyTag::WriteFile],
            AuditDecision::UserApproved,
        );

        assert_eq!(log.len(), 2);
        let recent = log.recent(10);
        assert_eq!(recent.len(), 2);
        assert_eq!(recent[0].tool_name, "bash");
        assert_eq!(recent[1].tool_name, "write");
    }

    #[test]
    fn recent_returns_last_n() {
        let mut log = AuditLog::new();
        for i in 0..10 {
            log.record(
                format!("tool_{i}"),
                "",
                RiskLevel::Safe,
                vec![],
                AuditDecision::AutoApproved,
            );
        }

        let recent = log.recent(3);
        assert_eq!(recent.len(), 3);
        assert_eq!(recent[0].tool_name, "tool_7");
        assert_eq!(recent[2].tool_name, "tool_9");
    }

    #[test]
    fn truncates_long_arguments() {
        let mut log = AuditLog::new();
        let long_arg = "a".repeat(300);
        log.record(
            "bash",
            long_arg,
            RiskLevel::Safe,
            vec![],
            AuditDecision::AutoApproved,
        );

        assert_eq!(log.recent(1)[0].arguments_summary.len(), 200);
    }

    #[test]
    fn max_entries_enforced() {
        let mut log = AuditLog::default();
        for i in 0..1005 {
            log.record(
                format!("tool_{i}"),
                "",
                RiskLevel::Safe,
                vec![],
                AuditDecision::AutoApproved,
            );
        }
        assert_eq!(log.len(), 1000);
        assert_eq!(log.recent(1)[0].tool_name, "tool_1004");
    }

    #[test]
    fn summary_counts() {
        let mut log = AuditLog::new();
        log.record(
            "a",
            "",
            RiskLevel::Safe,
            vec![],
            AuditDecision::AutoApproved,
        );
        log.record(
            "b",
            "",
            RiskLevel::Safe,
            vec![],
            AuditDecision::AutoApproved,
        );
        log.record("c", "", RiskLevel::Low, vec![], AuditDecision::UserApproved);
        log.record("d", "", RiskLevel::High, vec![], AuditDecision::Blocked);
        log.record(
            "e",
            "",
            RiskLevel::Medium,
            vec![],
            AuditDecision::UserDenied,
        );

        let summary = log.summary();
        assert_eq!(summary.total, 5);
        assert_eq!(summary.auto_approved, 2);
        assert_eq!(summary.user_approved, 1);
        assert_eq!(summary.blocked, 1);
        assert_eq!(summary.user_denied, 1);
    }

    #[test]
    fn empty_log() {
        let log = AuditLog::new();
        assert!(log.is_empty());
        assert_eq!(log.recent(5).len(), 0);
        assert_eq!(log.summary().total, 0);
    }
}
