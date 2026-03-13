use std::collections::{HashMap, HashSet};
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WorkerIntent {
    pub worker_id: Uuid,
    pub task: String,
    pub files: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ConflictReport {
    pub workers: (Uuid, Uuid),
    pub file_overlaps: Vec<String>,
}

#[derive(Debug, Default)]
pub struct ConflictDetector;

impl ConflictDetector {
    pub fn detect(intents: &[WorkerIntent]) -> Vec<ConflictReport> {
        let mut reports: Vec<ConflictReport> = Vec::new();
        let mut file_owners: HashMap<String, Uuid> = HashMap::new();
        let mut dedup: HashSet<(Uuid, Uuid, String)> = HashSet::new();

        for intent in intents {
            for file in &intent.files {
                if let Some(previous) = file_owners.get(file) {
                    if *previous == intent.worker_id {
                        continue;
                    }

                    let (left, right) = if *previous < intent.worker_id {
                        (*previous, intent.worker_id)
                    } else {
                        (intent.worker_id, *previous)
                    };

                    if !dedup.insert((left, right, file.clone())) {
                        continue;
                    }

                    if let Some(report) = reports
                        .iter_mut()
                        .find(|r| r.workers.0 == left && r.workers.1 == right)
                    {
                        report.file_overlaps.push(file.clone());
                    } else {
                        reports.push(ConflictReport {
                            workers: (left, right),
                            file_overlaps: vec![file.clone()],
                        });
                    }
                } else {
                    file_owners.insert(file.clone(), intent.worker_id);
                }
            }
        }

        reports
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn conflict_detector_finds_file_overlaps() {
        let w1 = Uuid::new_v4();
        let w2 = Uuid::new_v4();
        let w3 = Uuid::new_v4();
        let intents = vec![
            WorkerIntent {
                worker_id: w1,
                task: "Refactor auth".to_string(),
                files: vec!["src/auth.rs".to_string(), "src/session.rs".to_string()],
            },
            WorkerIntent {
                worker_id: w2,
                task: "Add logging".to_string(),
                files: vec!["src/auth.rs".to_string()],
            },
            WorkerIntent {
                worker_id: w3,
                task: "UI changes".to_string(),
                files: vec!["src/ui.rs".to_string()],
            },
        ];

        let reports = ConflictDetector::detect(&intents);
        assert_eq!(reports.len(), 1);
        assert_eq!(reports[0].file_overlaps, vec!["src/auth.rs".to_string()]);
    }

    #[test]
    fn conflict_detector_returns_empty_for_disjoint_files() {
        let intents = vec![
            WorkerIntent {
                worker_id: Uuid::new_v4(),
                task: "A".to_string(),
                files: vec!["a.rs".to_string()],
            },
            WorkerIntent {
                worker_id: Uuid::new_v4(),
                task: "B".to_string(),
                files: vec!["b.rs".to_string()],
            },
        ];

        assert!(ConflictDetector::detect(&intents).is_empty());
    }
}
