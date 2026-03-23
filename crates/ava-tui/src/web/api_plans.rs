//! Plan persistence API — list and load saved plans from `.ava/plans/`.

use axum::extract::Path;
use axum::response::IntoResponse;
use axum::Json;
use serde::Serialize;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanSummary {
    pub filename: String,
    pub codename: Option<String>,
    pub summary: String,
    pub step_count: usize,
    pub created: String,
}

/// `GET /api/plans` — List all saved plans from `.ava/plans/`.
pub async fn list_plans() -> impl IntoResponse {
    let plans_dir = std::path::PathBuf::from(".ava/plans");
    let mut plans = Vec::new();

    if let Ok(entries) = std::fs::read_dir(&plans_dir) {
        for entry in entries.flatten() {
            if let Some(name) = entry.file_name().to_str() {
                if name.ends_with(".md") {
                    if let Ok(content) = std::fs::read_to_string(entry.path()) {
                        let (codename, summary, step_count) = parse_plan_frontmatter(&content);
                        let created = name.split('-').take(2).collect::<Vec<_>>().join("-");
                        plans.push(PlanSummary {
                            filename: name.to_string(),
                            codename,
                            summary,
                            step_count,
                            created,
                        });
                    }
                }
            }
        }
    }

    plans.sort_by(|a, b| b.filename.cmp(&a.filename)); // newest first
    Json(plans)
}

/// `GET /api/plans/:filename` — Load a specific plan by filename.
pub async fn get_plan(Path(filename): Path<String>) -> impl IntoResponse {
    let path = std::path::PathBuf::from(".ava/plans").join(&filename);
    match std::fs::read_to_string(&path) {
        Ok(content) => (axum::http::StatusCode::OK, content).into_response(),
        Err(_) => (axum::http::StatusCode::NOT_FOUND, "Plan not found").into_response(),
    }
}

/// Parse plan frontmatter to extract codename, summary, and step count.
fn parse_plan_frontmatter(content: &str) -> (Option<String>, String, usize) {
    let mut codename = None;
    let mut summary = String::new();
    let mut step_count = 0;

    let mut in_frontmatter = false;
    for line in content.lines() {
        if line.trim() == "---" {
            if !in_frontmatter {
                in_frontmatter = true;
                continue;
            } else {
                in_frontmatter = false;
                continue;
            }
        }
        if in_frontmatter {
            if let Some(val) = line.strip_prefix("codename: ") {
                codename = Some(val.trim().to_string());
            }
            if let Some(val) = line.strip_prefix("summary: ") {
                summary = val.trim().trim_matches('"').to_string();
            }
        }
        if line.starts_with("### ") {
            step_count += 1;
        }
    }

    (codename, summary, step_count)
}
