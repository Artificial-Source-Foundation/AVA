use serde::{Deserialize, Serialize};
use sysinfo::{Pid, System};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LspSidebarEntry {
    pub name: String,
    pub status: String,
    pub detail: String,
}

pub fn refresh_lsp_entries(
    workspace: &std::path::Path,
    previous: &[LspSidebarEntry],
) -> Vec<LspSidebarEntry> {
    let mut system = System::new_all();
    system.refresh_all();

    let workspace_str = workspace.to_string_lossy().to_string();
    let mut entries = Vec::new();
    let previous_by_name: std::collections::HashMap<&str, &LspSidebarEntry> = previous
        .iter()
        .map(|entry| (entry.name.as_str(), entry))
        .collect();

    for spec in known_servers() {
        let mut matches: Vec<(Pid, String)> = Vec::new();

        for (pid, process) in system.processes() {
            let name = process.name().to_ascii_lowercase();
            let cmd = process
                .cmd()
                .iter()
                .map(|part| part.to_string())
                .collect::<Vec<_>>();
            let cmd_joined = cmd.join(" ").to_ascii_lowercase();

            let name_match = spec
                .binary_names
                .iter()
                .any(|candidate| name.contains(candidate) || cmd_joined.contains(candidate));
            if !name_match {
                continue;
            }

            if let Some(required_arg) = spec.required_arg {
                if !cmd_joined.contains(required_arg) {
                    continue;
                }
            }

            let cwd_match = process
                .cwd()
                .map(|cwd| {
                    let cwd = cwd.to_string_lossy();
                    cwd.starts_with(&workspace_str) || workspace_str.starts_with(cwd.as_ref())
                })
                .unwrap_or(false);
            let cmd_match = cmd
                .iter()
                .any(|part: &String| part.contains(&workspace_str));

            if cwd_match || cmd_match {
                matches.push((*pid, status_for_process(process)));
            }
        }

        if let Some((pid, process_state)) = matches.into_iter().next() {
            let status = if let Some(previous) = previous_by_name.get(spec.label) {
                if previous.status == "starting" || previous.status == "restarting" {
                    "connected".to_string()
                } else {
                    process_state.clone()
                }
            } else if process_state == "idle" {
                "starting".to_string()
            } else {
                "connected".to_string()
            };
            entries.push(LspSidebarEntry {
                name: spec.label.to_string(),
                status,
                detail: format!("pid {}", pid.as_u32()),
            });
        } else if let Some(previous) = previous_by_name.get(spec.label) {
            let status = match previous.status.as_str() {
                "connected" | "idle" | "starting" => "restarting",
                "restarting" => "failed",
                "failed" => "failed",
                other => other,
            };
            entries.push(LspSidebarEntry {
                name: spec.label.to_string(),
                status: status.to_string(),
                detail: format!("last seen {}", previous.detail),
            });
        }
    }

    entries
}

fn status_for_process(process: &sysinfo::Process) -> String {
    let cpu = process.cpu_usage();
    if cpu > 0.2 {
        "live".to_string()
    } else {
        "idle".to_string()
    }
}

struct LspSpec {
    label: &'static str,
    binary_names: &'static [&'static str],
    required_arg: Option<&'static str>,
}

fn known_servers() -> &'static [LspSpec] {
    &[
        LspSpec {
            label: "rust-analyzer",
            binary_names: &["rust-analyzer"],
            required_arg: None,
        },
        LspSpec {
            label: "typescript",
            binary_names: &["typescript-language-server", "vtsls"],
            required_arg: None,
        },
        LspSpec {
            label: "eslint",
            binary_names: &["eslint-language-server", "vscode-eslint-language-server"],
            required_arg: None,
        },
        LspSpec {
            label: "biome",
            binary_names: &["biome"],
            required_arg: Some("lsp"),
        },
        LspSpec {
            label: "python",
            binary_names: &[
                "pyright-langserver",
                "basedpyright-langserver",
                "pylsp",
                "ruff",
            ],
            required_arg: None,
        },
        LspSpec {
            label: "gopls",
            binary_names: &["gopls"],
            required_arg: None,
        },
        LspSpec {
            label: "clangd",
            binary_names: &["clangd"],
            required_arg: None,
        },
    ]
}
