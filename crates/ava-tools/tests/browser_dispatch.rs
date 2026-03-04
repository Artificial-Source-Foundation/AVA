use ava_tools::browser::{BrowserDriver, BrowserEngine, BrowserError, BrowserResult};
use std::cell::RefCell;

#[derive(Default)]
struct MockBrowserDriver {
    calls: RefCell<Vec<String>>,
}

impl MockBrowserDriver {
    fn called(&self, expected: &str) -> bool {
        self.calls.borrow().iter().any(|call| call == expected)
    }
}

impl BrowserDriver for MockBrowserDriver {
    fn navigate(&self, url: &str) -> Result<BrowserResult, BrowserError> {
        self.calls.borrow_mut().push(format!("navigate:{url}"));
        Ok(BrowserResult::new("navigated"))
    }

    fn click(&self, selector: &str) -> Result<BrowserResult, BrowserError> {
        self.calls.borrow_mut().push(format!("click:{selector}"));
        Ok(BrowserResult::new("clicked"))
    }

    fn type_text(&self, selector: &str, text: &str) -> Result<BrowserResult, BrowserError> {
        self.calls
            .borrow_mut()
            .push(format!("type:{selector}:{text}"));
        Ok(BrowserResult::new("typed"))
    }

    fn extract_text(&self, selector: &str) -> Result<BrowserResult, BrowserError> {
        self.calls.borrow_mut().push(format!("extract:{selector}"));
        Ok(BrowserResult::new("value"))
    }

    fn screenshot(&self, path: &str) -> Result<BrowserResult, BrowserError> {
        self.calls.borrow_mut().push(format!("screenshot:{path}"));
        Ok(BrowserResult::new("saved"))
    }
}

#[test]
fn dispatches_navigate_action() {
    let driver = MockBrowserDriver::default();
    let engine = BrowserEngine::new(&driver);

    let result = engine
        .dispatch_from_json(r#"{"action":"navigate","url":"https://example.com"}"#)
        .expect("navigate should succeed");

    assert_eq!(result.output, "navigated");
    assert!(driver.called("navigate:https://example.com"));
}

#[test]
fn dispatches_click_action() {
    let driver = MockBrowserDriver::default();
    let engine = BrowserEngine::new(&driver);

    let result = engine
        .dispatch_from_json(r##"{"action":"click","selector":"#submit"}"##)
        .expect("click should succeed");

    assert_eq!(result.output, "clicked");
    assert!(driver.called("click:#submit"));
}

#[test]
fn dispatches_type_action() {
    let driver = MockBrowserDriver::default();
    let engine = BrowserEngine::new(&driver);

    let result = engine
        .dispatch_from_json(r##"{"action":"type","selector":"#query","text":"ava"}"##)
        .expect("type should succeed");

    assert_eq!(result.output, "typed");
    assert!(driver.called("type:#query:ava"));
}

#[test]
fn dispatches_extract_action() {
    let driver = MockBrowserDriver::default();
    let engine = BrowserEngine::new(&driver);

    let result = engine
        .dispatch_from_json(r#"{"action":"extract","selector":"h1"}"#)
        .expect("extract should succeed");

    assert_eq!(result.output, "value");
    assert!(driver.called("extract:h1"));
}

#[test]
fn dispatches_screenshot_action() {
    let driver = MockBrowserDriver::default();
    let engine = BrowserEngine::new(&driver);

    let result = engine
        .dispatch_from_json(r#"{"action":"screenshot","path":"shot.png"}"#)
        .expect("screenshot should succeed");

    assert_eq!(result.output, "saved");
    assert!(driver.called("screenshot:shot.png"));
}

#[test]
fn rejects_invalid_action() {
    let driver = MockBrowserDriver::default();
    let engine = BrowserEngine::new(&driver);

    let error = engine
        .dispatch_from_json(r##"{"action":"hover","selector":"#item"}"##)
        .expect_err("unsupported actions should fail");

    assert!(matches!(error, BrowserError::UnsupportedAction(action) if action == "hover"));
}
