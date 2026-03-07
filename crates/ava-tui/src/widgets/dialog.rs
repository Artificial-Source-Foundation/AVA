#[derive(Debug, Clone)]
pub struct DialogState {
    pub title: String,
    pub body: String,
    pub open: bool,
}

impl DialogState {
    pub fn new(title: impl Into<String>, body: impl Into<String>) -> Self {
        Self {
            title: title.into(),
            body: body.into(),
            open: true,
        }
    }
}
