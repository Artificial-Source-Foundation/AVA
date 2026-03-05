use std::collections::HashSet;

use ava_codebase::repomap::{generate_repomap, select_relevant_files, RepoFile};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ComputeRepoMapFileInput {
    pub path: String,
    pub content: String,
    #[serde(default)]
    pub dependencies: Vec<String>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ComputeRepoMapInput {
    pub files: Vec<ComputeRepoMapFileInput>,
    #[serde(default)]
    pub query: String,
    pub limit: Option<usize>,
    #[serde(default)]
    pub active_files: Vec<String>,
    #[serde(default)]
    pub mentioned_files: Vec<String>,
    #[serde(default)]
    pub private_files: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RepoMapEntry {
    pub path: String,
    pub score: f64,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ComputeRepoMapOutput {
    pub files: Vec<RepoMapEntry>,
}

fn is_private_path(path: &str) -> bool {
    path.split('/').any(|segment| segment.starts_with('_'))
}

#[tauri::command]
pub fn compute_repo_map(input: ComputeRepoMapInput) -> Result<ComputeRepoMapOutput, String> {
    let files: Vec<RepoFile> = input
        .files
        .iter()
        .map(|file| RepoFile {
            path: file.path.clone(),
            content: file.content.clone(),
            dependencies: file.dependencies.clone(),
        })
        .collect();

    let ranked = generate_repomap(&files, &input.query);
    let mut selected = select_relevant_files(&ranked, input.limit.unwrap_or(100));

    let active: HashSet<String> = input.active_files.into_iter().collect();
    let mentioned: HashSet<String> = input.mentioned_files.into_iter().collect();
    let private: HashSet<String> = input.private_files.into_iter().collect();

    for file in &mut selected {
        if active.contains(&file.path) {
            file.score *= 50.0;
        }
        if mentioned.contains(&file.path) {
            file.score *= 10.0;
        }
        if private.contains(&file.path) || is_private_path(&file.path) {
            file.score *= 0.1;
        }
    }

    selected.sort_by(|a, b| b.score.total_cmp(&a.score));

    Ok(ComputeRepoMapOutput {
        files: selected
            .into_iter()
            .map(|entry| RepoMapEntry {
                path: entry.path,
                score: entry.score,
            })
            .collect(),
    })
}
