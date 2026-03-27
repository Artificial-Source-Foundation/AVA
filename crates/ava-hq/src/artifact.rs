use serde::{Deserialize, Serialize};
use std::time::{SystemTime, UNIX_EPOCH};
use uuid::Uuid;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum ArtifactKind {
    WorkflowPhaseOutput,
    WorkflowSummary,
    ReviewReport,
    Custom(String),
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Artifact {
    pub id: Uuid,
    pub created_at_unix_secs: u64,
    pub kind: ArtifactKind,
    pub producer: String,
    pub title: String,
    pub content: String,
    pub spec_id: Option<Uuid>,
}

impl Artifact {
    pub fn new(
        kind: ArtifactKind,
        producer: impl Into<String>,
        title: impl Into<String>,
        content: impl Into<String>,
        spec_id: Option<Uuid>,
    ) -> Self {
        Self {
            id: Uuid::new_v4(),
            created_at_unix_secs: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_secs())
                .unwrap_or(0),
            kind,
            producer: producer.into(),
            title: title.into(),
            content: content.into(),
            spec_id,
        }
    }

    pub fn workflow_phase(
        phase_name: impl Into<String>,
        role: impl Into<String>,
        output: impl Into<String>,
        spec_id: Option<Uuid>,
    ) -> Self {
        let phase_name = phase_name.into();
        let role = role.into();
        Self::new(
            ArtifactKind::WorkflowPhaseOutput,
            format!("workflow:{role}"),
            format!("{phase_name} output"),
            output,
            spec_id,
        )
    }

    pub fn workflow_summary(
        workflow_name: impl Into<String>,
        summary: impl Into<String>,
        spec_id: Option<Uuid>,
    ) -> Self {
        let workflow_name = workflow_name.into();
        Self::new(
            ArtifactKind::WorkflowSummary,
            "workflow-executor",
            format!("{workflow_name} summary"),
            summary,
            spec_id,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn artifact_builders_fill_expected_fields() {
        let spec_id = Uuid::new_v4();
        let phase = Artifact::workflow_phase("Code", "Coder", "Implemented auth", Some(spec_id));

        assert_eq!(phase.kind, ArtifactKind::WorkflowPhaseOutput);
        assert_eq!(phase.spec_id, Some(spec_id));
        assert!(phase.title.contains("Code"));
        assert!(phase.producer.contains("Coder"));

        let summary = Artifact::workflow_summary("plan-code-review", "All phases completed", None);
        assert_eq!(summary.kind, ArtifactKind::WorkflowSummary);
        assert_eq!(summary.spec_id, None);
    }
}
