use ava_tools::browser::{BrowserDriver, BrowserEngine, BrowserError, BrowserResult};
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct BrowserCommandOutput {
    pub output: String,
}

struct AdapterBrowserDriver;

impl BrowserDriver for AdapterBrowserDriver {
    fn navigate(&self, url: &str) -> Result<BrowserResult, BrowserError> {
        Ok(BrowserResult::new(format!("navigated:{url}")))
    }

    fn click(&self, selector: &str) -> Result<BrowserResult, BrowserError> {
        Ok(BrowserResult::new(format!("clicked:{selector}")))
    }

    fn type_text(&self, selector: &str, text: &str) -> Result<BrowserResult, BrowserError> {
        Ok(BrowserResult::new(format!("typed:{selector}:{text}")))
    }

    fn extract_text(&self, selector: &str) -> Result<BrowserResult, BrowserError> {
        Ok(BrowserResult::new(format!("extracted:{selector}")))
    }

    fn screenshot(&self, path: &str) -> Result<BrowserResult, BrowserError> {
        Ok(BrowserResult::new(format!("screenshot:{path}")))
    }
}

impl From<BrowserResult> for BrowserCommandOutput {
    fn from(value: BrowserResult) -> Self {
        Self {
            output: value.output,
        }
    }
}

fn run_browser_payload(payload: &str) -> Result<BrowserCommandOutput, String> {
    let engine = BrowserEngine::new(&AdapterBrowserDriver);
    engine
        .dispatch_from_json(payload)
        .map(BrowserCommandOutput::from)
        .map_err(|err| err.to_string())
}

#[tauri::command]
pub fn execute_browser_tool(payload: String) -> Result<BrowserCommandOutput, String> {
    run_browser_payload(&payload)
}

#[cfg(test)]
mod tests {
    use super::{execute_browser_tool, run_browser_payload};

    #[test]
    fn execute_browser_tool_maps_json_and_returns_serializable_output() {
        let result = execute_browser_tool(
            r#"{"action":"navigate","url":"https://example.com"}"#.to_string(),
        );
        let output = result.expect("browser command should execute");
        let json_value = serde_json::to_value(&output).expect("output should serialize");

        assert_eq!(json_value["output"], "navigated:https://example.com");
    }

    #[test]
    fn browser_payload_rejects_invalid_actions() {
        let error = run_browser_payload(r#"{"action":"zoom"}"#).expect_err("must fail");
        assert!(error.contains("unsupported browser action"));
    }
}
