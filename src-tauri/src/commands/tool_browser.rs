use ava_tools::browser::{BrowserDriver, BrowserEngine, BrowserError, BrowserResult};
use serde::Serialize;

const MCP_BROWSER_MESSAGE: &str =
    "Browser automation requires an MCP server. Configure a Puppeteer or Playwright MCP server in settings.";

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct BrowserCommandOutput {
    pub output: String,
}

struct AdapterBrowserDriver;

impl BrowserDriver for AdapterBrowserDriver {
    fn navigate(&self, _url: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn click(&self, _selector: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn type_text(&self, _selector: &str, _text: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn extract_text(&self, _selector: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
    }

    fn screenshot(&self, _path: &str) -> Result<BrowserResult, BrowserError> {
        Err(BrowserError::Driver(MCP_BROWSER_MESSAGE.to_string()))
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
    fn execute_browser_tool_returns_mcp_error() {
        let error = execute_browser_tool(
            r#"{"action":"navigate","url":"https://example.com"}"#.to_string(),
        )
        .expect_err("browser command should fail without MCP server");

        assert!(error.contains("requires an MCP server"));
    }

    #[test]
    fn browser_payload_rejects_invalid_actions() {
        let error = run_browser_payload(r#"{"action":"zoom"}"#).expect_err("must fail");
        assert!(error.contains("unsupported browser action"));
    }
}
