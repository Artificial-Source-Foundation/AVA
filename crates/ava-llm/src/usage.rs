//! Subscription usage tracking for OAuth-based and credit-based providers.
//!
//! Queries provider APIs to show plan type, usage windows, and remaining credits.
//! Each provider fetcher is fail-soft: errors are captured in the response rather
//! than propagated, so one provider failing does not block the others.

use ava_config::credentials::CredentialStore;
use serde::{Deserialize, Serialize};
use tracing::debug;

/// A usage snapshot for a single provider.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SubscriptionUsage {
    pub provider: String,
    pub display_name: String,
    pub plan_type: Option<String>,
    pub usage_windows: Vec<UsageWindow>,
    pub credits: Option<CreditsInfo>,
    pub copilot_quota: Option<CopilotQuota>,
    pub error: Option<String>,
}

/// A rate-limit or usage window (e.g., 5-hour, 7-day).
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UsageWindow {
    pub label: String,
    pub used_percent: f64,
    pub resets_at: Option<String>,
}

/// Credit balance info for credit-based providers.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CreditsInfo {
    pub has_credits: bool,
    pub unlimited: bool,
    pub balance: Option<String>,
}

/// Copilot-specific quota info.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CopilotQuota {
    pub remaining: i64,
    pub limit: i64,
    pub percent_remaining: f64,
    pub reset_time: Option<String>,
    pub completions_remaining: Option<i64>,
    pub completions_limit: Option<i64>,
}

/// Fetch usage from all supported subscription providers concurrently.
pub async fn fetch_all_subscription_usage(credentials: &CredentialStore) -> Vec<SubscriptionUsage> {
    let (codex, copilot, openrouter) = tokio::join!(
        fetch_codex_usage(credentials),
        fetch_copilot_usage(credentials),
        fetch_openrouter_usage(credentials),
    );
    vec![codex, copilot, openrouter]
}

fn http_client() -> reqwest::Client {
    reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(10))
        .build()
        .unwrap_or_default()
}

// ---------------------------------------------------------------------------
// OpenAI / Codex (ChatGPT subscription)
// ---------------------------------------------------------------------------

async fn fetch_codex_usage(credentials: &CredentialStore) -> SubscriptionUsage {
    let base = SubscriptionUsage {
        provider: "openai".into(),
        display_name: "OpenAI".into(),
        plan_type: None,
        usage_windows: vec![],
        credits: None,
        copilot_quota: None,
        error: None,
    };

    let cred = match credentials.get("openai") {
        Some(c) if c.is_oauth_configured() => c,
        _ => {
            return SubscriptionUsage {
                error: Some("Not configured (OAuth required)".into()),
                ..base
            }
        }
    };

    let Some(token) = cred.oauth_token.as_deref() else {
        return SubscriptionUsage {
            error: Some("No OAuth token".into()),
            ..base
        };
    };

    let client = http_client();
    let mut req = client
        .get("https://chatgpt.com/backend-api/wham/usage")
        .header("Authorization", format!("Bearer {token}"));

    if let Some(ref account_id) = cred.oauth_account_id {
        req = req.header("ChatGPT-Account-Id", account_id);
    }

    let resp = match req.send().await {
        Ok(r) if r.status().is_success() => r,
        Ok(r) => {
            return SubscriptionUsage {
                error: Some(format!("HTTP {}", r.status())),
                ..base
            }
        }
        Err(e) => {
            return SubscriptionUsage {
                error: Some(e.to_string()),
                ..base
            }
        }
    };

    let data: serde_json::Value = match resp.json().await {
        Ok(v) => v,
        Err(e) => {
            return SubscriptionUsage {
                error: Some(e.to_string()),
                ..base
            }
        }
    };

    debug!("codex usage response: {data}");

    let plan_type = data
        .get("plan_type")
        .and_then(|v| v.as_str())
        .map(String::from);

    let mut windows = vec![];

    // Global rate limits (5 hour + weekly)
    if let Some(rl) = data.get("rate_limit") {
        if let Some(primary) = rl.get("primary_window") {
            let label = primary
                .get("label")
                .and_then(|v| v.as_str())
                .unwrap_or("5 hour usage limit");
            if let Some(w) = parse_codex_window(primary, label) {
                windows.push(w);
            }
        }
        if let Some(secondary) = rl.get("secondary_window") {
            let label = secondary
                .get("label")
                .and_then(|v| v.as_str())
                .unwrap_or("Weekly usage limit");
            if let Some(w) = parse_codex_window(secondary, label) {
                windows.push(w);
            }
        }
    }

    // Per-model rate limits (e.g. GPT-5.3-Codex-Spark 5 hour / weekly)
    if let Some(models) = data
        .get("per_model_rate_limits")
        .and_then(|v| v.as_object())
    {
        for (model_name, model_rl) in models {
            if let Some(primary) = model_rl.get("primary_window") {
                let label = primary
                    .get("label")
                    .and_then(|v| v.as_str())
                    .map(String::from)
                    .unwrap_or_else(|| format!("{model_name} 5 hour usage limit"));
                if let Some(w) = parse_codex_window(primary, &label) {
                    windows.push(w);
                }
            }
            if let Some(secondary) = model_rl.get("secondary_window") {
                let label = secondary
                    .get("label")
                    .and_then(|v| v.as_str())
                    .map(String::from)
                    .unwrap_or_else(|| format!("{model_name} Weekly usage limit"));
                if let Some(w) = parse_codex_window(secondary, &label) {
                    windows.push(w);
                }
            }
        }
    }

    // Intentionally skip code_review_rate_limit and credits — not relevant for coding agents

    SubscriptionUsage {
        plan_type,
        usage_windows: windows,
        ..base
    }
}

fn parse_codex_window(v: &serde_json::Value, label: &str) -> Option<UsageWindow> {
    let used_percent = v.get("used_percent")?.as_f64()?;
    let resets_at = v
        .get("reset_at")
        .and_then(|r| r.as_i64())
        .map(|ts| ts.to_string());
    Some(UsageWindow {
        label: label.to_string(),
        used_percent,
        resets_at,
    })
}

// ---------------------------------------------------------------------------
// GitHub Copilot
// ---------------------------------------------------------------------------

async fn fetch_copilot_usage(credentials: &CredentialStore) -> SubscriptionUsage {
    let base = SubscriptionUsage {
        provider: "copilot".into(),
        display_name: "GitHub Copilot".into(),
        plan_type: None,
        usage_windows: vec![],
        credits: None,
        copilot_quota: None,
        error: None,
    };

    let cred = match credentials.get("copilot") {
        Some(c) if c.is_oauth_configured() => c,
        _ => {
            return SubscriptionUsage {
                error: Some("Not configured (OAuth required)".into()),
                ..base
            }
        }
    };

    let oauth_token = match cred.oauth_token.as_deref() {
        Some(t) => t.to_string(),
        None => {
            return SubscriptionUsage {
                error: Some("No OAuth token".into()),
                ..base
            }
        }
    };

    let client = http_client();
    let copilot_headers = [
        ("User-Agent", "GitHubCopilotChat/0.35.0"),
        ("Editor-Version", "vscode/1.107.0"),
        ("Editor-Plugin-Version", "copilot-chat/0.35.0"),
        ("Copilot-Integration-Id", "vscode-chat"),
        ("Accept", "application/json"),
    ];

    // Try direct token first, then exchange for Copilot token
    let mut resp = {
        let mut r = client
            .get("https://api.github.com/copilot_internal/user")
            .header("Authorization", format!("token {oauth_token}"));
        for (k, v) in &copilot_headers {
            r = r.header(*k, *v);
        }
        r.send().await
    };

    if resp
        .as_ref()
        .map(|r| !r.status().is_success())
        .unwrap_or(true)
    {
        // Exchange for Copilot API token
        let exchange_resp = {
            let mut r = client
                .get("https://api.github.com/copilot_internal/v2/token")
                .header("Authorization", format!("Bearer {oauth_token}"))
                .header("Accept", "application/json");
            for (k, v) in &copilot_headers {
                r = r.header(*k, *v);
            }
            r.send().await
        };

        if let Ok(ex) = exchange_resp {
            if ex.status().is_success() {
                if let Ok(token_data) = ex.json::<serde_json::Value>().await {
                    if let Some(copilot_token) = token_data.get("token").and_then(|v| v.as_str()) {
                        let mut r = client
                            .get("https://api.github.com/copilot_internal/user")
                            .header("Authorization", format!("Bearer {copilot_token}"));
                        for (k, v) in &copilot_headers {
                            r = r.header(*k, *v);
                        }
                        resp = r.send().await;
                    }
                }
            }
        }
    }

    let data: serde_json::Value = match resp {
        Ok(r) if r.status().is_success() => match r.json().await {
            Ok(v) => v,
            Err(e) => {
                return SubscriptionUsage {
                    error: Some(e.to_string()),
                    ..base
                }
            }
        },
        Ok(r) => {
            return SubscriptionUsage {
                error: Some(format!("HTTP {}", r.status())),
                ..base
            }
        }
        Err(e) => {
            return SubscriptionUsage {
                error: Some(e.to_string()),
                ..base
            }
        }
    };

    debug!("copilot usage response: {data}");

    // Extract plan type
    let plan_type = data.get("copilot_plan").and_then(|v| v.as_str()).map(|s| {
        let mut chars = s.chars();
        match chars.next() {
            Some(c) => format!("{}{}", c.to_uppercase(), chars.as_str()),
            None => s.to_string(),
        }
    });

    let reset_time = data
        .get("quota_reset_date")
        .and_then(|v| v.as_str())
        .map(String::from);

    // Parse quota_snapshots (modern API format)
    if let Some(snapshots) = data.get("quota_snapshots") {
        let chat = snapshots.get("chat");
        let completions = snapshots.get("completions");
        let premium = snapshots.get("premium_interactions");

        // Chat quota
        let _chat_unlimited = chat
            .and_then(|c| c.get("unlimited"))
            .and_then(|v| v.as_bool())
            .unwrap_or(false);

        // Completions quota
        let comp_unlimited = completions
            .and_then(|c| c.get("unlimited"))
            .and_then(|v| v.as_bool())
            .unwrap_or(false);
        let comp_remaining = completions
            .and_then(|c| c.get("remaining"))
            .and_then(|v| v.as_i64());
        let comp_entitlement = completions
            .and_then(|c| c.get("entitlement"))
            .and_then(|v| v.as_i64());

        // Premium interactions (the main quota for paid plans)
        let prem_unlimited = premium
            .and_then(|p| p.get("unlimited"))
            .and_then(|v| v.as_bool())
            .unwrap_or(false);
        let prem_entitlement = premium
            .and_then(|p| p.get("entitlement"))
            .and_then(|v| v.as_i64())
            .unwrap_or(0);
        let prem_remaining = premium
            .and_then(|p| p.get("remaining"))
            .and_then(|v| v.as_i64())
            .unwrap_or(0);
        let prem_percent = premium
            .and_then(|p| p.get("percent_remaining"))
            .and_then(|v| v.as_f64())
            .unwrap_or(0.0);

        let (limit, remaining, percent_remaining) = if prem_unlimited {
            (-1, 0, 100.0)
        } else {
            (prem_entitlement, prem_remaining, prem_percent)
        };

        return SubscriptionUsage {
            plan_type,
            copilot_quota: Some(CopilotQuota {
                remaining,
                limit,
                percent_remaining: percent_remaining.clamp(0.0, 100.0),
                reset_time,
                completions_remaining: if comp_unlimited {
                    Some(-1)
                } else {
                    comp_remaining
                },
                completions_limit: if comp_unlimited {
                    Some(-1)
                } else {
                    comp_entitlement
                },
            }),
            ..base
        };
    }

    // Fallback: limited_user_quotas (legacy free tier format)
    if let Some(quotas) = data.get("limited_user_quotas") {
        let chat_remaining = quotas.get("chat").and_then(|v| v.as_i64()).unwrap_or(0);
        let monthly = data.get("monthly_quotas");
        let chat_total = monthly
            .and_then(|m| m.get("chat"))
            .and_then(|v| v.as_i64())
            .unwrap_or(0);

        let scale = if chat_total == 500 { 10 } else { 1 };
        let remaining = chat_remaining / scale;
        let total = chat_total / scale;

        return SubscriptionUsage {
            plan_type,
            copilot_quota: Some(CopilotQuota {
                remaining,
                limit: total,
                percent_remaining: if total > 0 {
                    (remaining as f64 / total as f64 * 100.0).clamp(0.0, 100.0)
                } else {
                    0.0
                },
                reset_time,
                completions_remaining: None,
                completions_limit: None,
            }),
            ..base
        };
    }

    SubscriptionUsage {
        plan_type,
        error: Some("Could not parse quota data".into()),
        ..base
    }
}

// ---------------------------------------------------------------------------
// OpenRouter (API key, not OAuth)
// ---------------------------------------------------------------------------

async fn fetch_openrouter_usage(credentials: &CredentialStore) -> SubscriptionUsage {
    let base = SubscriptionUsage {
        provider: "openrouter".into(),
        display_name: "OpenRouter".into(),
        plan_type: None,
        usage_windows: vec![],
        credits: None,
        copilot_quota: None,
        error: None,
    };

    let Some(cred) = credentials.get("openrouter") else {
        return SubscriptionUsage {
            error: Some("Not configured".into()),
            ..base
        };
    };

    let Some(api_key) = cred.effective_api_key().map(str::to_string) else {
        return SubscriptionUsage {
            error: Some("No API key".into()),
            ..base
        };
    };

    let client = http_client();
    let resp = client
        .get("https://openrouter.ai/api/v1/key")
        .header("Authorization", format!("Bearer {api_key}"))
        .send()
        .await;

    let data: serde_json::Value = match resp {
        Ok(r) if r.status().is_success() => match r.json().await {
            Ok(v) => v,
            Err(e) => {
                return SubscriptionUsage {
                    error: Some(e.to_string()),
                    ..base
                }
            }
        },
        Ok(r) => {
            return SubscriptionUsage {
                error: Some(format!("HTTP {}", r.status())),
                ..base
            }
        }
        Err(e) => {
            return SubscriptionUsage {
                error: Some(e.to_string()),
                ..base
            }
        }
    };

    debug!("openrouter usage response: {data}");

    let info = data.get("data").unwrap_or(&data);
    let is_free = info
        .get("is_free_tier")
        .and_then(|v| v.as_bool())
        .unwrap_or(false);
    let limit = info.get("limit").and_then(|v| v.as_f64());
    let usage = info.get("usage").and_then(|v| v.as_f64()).unwrap_or(0.0);
    let limit_remaining = info.get("limit_remaining").and_then(|v| v.as_f64());

    let plan_type = Some(if is_free {
        "Free".into()
    } else {
        "Paid".into()
    });

    let mut windows = vec![];
    if let Some(lim) = limit {
        if lim > 0.0 {
            windows.push(UsageWindow {
                label: "Credit Usage".into(),
                used_percent: ((usage / lim) * 100.0).clamp(0.0, 100.0),
                resets_at: info
                    .get("limit_reset")
                    .and_then(|v| v.as_str())
                    .map(String::from),
            });
        }
    }

    let balance_str = match limit_remaining {
        Some(r) => format!("${r:.2}"),
        None => "Unlimited".into(),
    };

    SubscriptionUsage {
        plan_type,
        usage_windows: windows,
        credits: Some(CreditsInfo {
            has_credits: true,
            unlimited: limit.is_none(),
            balance: Some(balance_str),
        }),
        ..base
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Live integration test — requires real credentials at ~/.ava/credentials.json.
    /// Run with: cargo test -p ava-llm usage::tests::live_fetch -- --ignored --nocapture
    #[tokio::test]
    #[ignore]
    async fn live_fetch() {
        let credentials = CredentialStore::load_default().await.unwrap();
        println!(
            "Configured providers: {:?}",
            credentials.configured_providers()
        );

        let results = fetch_all_subscription_usage(&credentials).await;
        assert_eq!(results.len(), 3);

        for u in &results {
            println!("\n--- {} ({}) ---", u.display_name, u.provider);
            if let Some(ref plan) = u.plan_type {
                println!("  Plan: {plan}");
            }
            for w in &u.usage_windows {
                println!(
                    "  {}: {:.1}% used (resets: {:?})",
                    w.label, w.used_percent, w.resets_at
                );
            }
            if let Some(ref c) = u.credits {
                println!(
                    "  Credits: has={}, unlimited={}, balance={:?}",
                    c.has_credits, c.unlimited, c.balance
                );
            }
            if let Some(ref q) = u.copilot_quota {
                println!(
                    "  Copilot: {}/{} remaining ({:.0}%), resets: {:?}",
                    q.remaining, q.limit, q.percent_remaining, q.reset_time
                );
                if let (Some(cr), Some(cl)) = (q.completions_remaining, q.completions_limit) {
                    println!("  Completions: {cr}/{cl}");
                }
            }
            if let Some(ref e) = u.error {
                println!("  Error: {e}");
            }
        }
    }
}
