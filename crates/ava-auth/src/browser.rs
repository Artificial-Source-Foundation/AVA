//! Browser opening utility.

use crate::AuthError;

/// Open a URL in the user's default browser.
pub fn open_browser(url: &str) -> Result<(), AuthError> {
    open::that(url).map_err(|e| AuthError::BrowserOpen(e.to_string()))
}
