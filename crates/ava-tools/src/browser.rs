use serde::Deserialize;
use thiserror::Error;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum BrowserAction {
    Navigate { url: String },
    Click { selector: String },
    Type { selector: String, text: String },
    Extract { selector: String },
    Screenshot { path: String },
}

impl BrowserAction {
    pub fn from_json(payload: &str) -> Result<Self, BrowserError> {
        let raw: RawBrowserAction = serde_json::from_str(payload)
            .map_err(|err| BrowserError::InvalidActionPayload(err.to_string()))?;

        raw.into_action()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BrowserResult {
    pub output: String,
}

impl BrowserResult {
    pub fn new(output: impl Into<String>) -> Self {
        Self {
            output: output.into(),
        }
    }
}

pub trait BrowserDriver {
    fn navigate(&self, url: &str) -> Result<BrowserResult, BrowserError>;
    fn click(&self, selector: &str) -> Result<BrowserResult, BrowserError>;
    fn type_text(&self, selector: &str, text: &str) -> Result<BrowserResult, BrowserError>;
    fn extract_text(&self, selector: &str) -> Result<BrowserResult, BrowserError>;
    fn screenshot(&self, path: &str) -> Result<BrowserResult, BrowserError>;
}

pub struct BrowserEngine<'a, D: BrowserDriver> {
    driver: &'a D,
}

impl<'a, D: BrowserDriver> BrowserEngine<'a, D> {
    pub fn new(driver: &'a D) -> Self {
        Self { driver }
    }

    pub fn dispatch_from_json(&self, payload: &str) -> Result<BrowserResult, BrowserError> {
        let action = BrowserAction::from_json(payload)?;
        self.dispatch(action)
    }

    pub fn dispatch(&self, action: BrowserAction) -> Result<BrowserResult, BrowserError> {
        match action {
            BrowserAction::Navigate { url } => self.driver.navigate(&url),
            BrowserAction::Click { selector } => self.driver.click(&selector),
            BrowserAction::Type { selector, text } => self.driver.type_text(&selector, &text),
            BrowserAction::Extract { selector } => self.driver.extract_text(&selector),
            BrowserAction::Screenshot { path } => self.driver.screenshot(&path),
        }
    }
}

#[derive(Debug, Error)]
pub enum BrowserError {
    #[error("invalid browser action payload: {0}")]
    InvalidActionPayload(String),
    #[error("unsupported browser action: {0}")]
    UnsupportedAction(String),
    #[error("missing field '{field}' for action '{action}'")]
    MissingField { action: String, field: &'static str },
    #[error("driver error: {0}")]
    Driver(String),
}

#[derive(Debug, Deserialize)]
struct RawBrowserAction {
    action: String,
    url: Option<String>,
    selector: Option<String>,
    text: Option<String>,
    path: Option<String>,
}

impl RawBrowserAction {
    fn into_action(self) -> Result<BrowserAction, BrowserError> {
        match self.action.as_str() {
            "navigate" => Ok(BrowserAction::Navigate {
                url: required_field(self.url, "navigate", "url")?,
            }),
            "click" => Ok(BrowserAction::Click {
                selector: required_field(self.selector, "click", "selector")?,
            }),
            "type" => Ok(BrowserAction::Type {
                selector: required_field(self.selector, "type", "selector")?,
                text: required_field(self.text, "type", "text")?,
            }),
            "extract" => Ok(BrowserAction::Extract {
                selector: required_field(self.selector, "extract", "selector")?,
            }),
            "screenshot" => Ok(BrowserAction::Screenshot {
                path: required_field(self.path, "screenshot", "path")?,
            }),
            _ => Err(BrowserError::UnsupportedAction(self.action)),
        }
    }
}

fn required_field(
    value: Option<String>,
    action: &'static str,
    field: &'static str,
) -> Result<String, BrowserError> {
    value.ok_or(BrowserError::MissingField {
        action: action.to_string(),
        field,
    })
}
