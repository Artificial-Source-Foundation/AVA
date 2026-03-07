#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AutocompleteTrigger {
    Slash,
    AtMention,
}

#[derive(Debug, Clone)]
pub struct AutocompleteItem {
    pub value: String,
    pub detail: String,
}

impl AutocompleteItem {
    pub fn new(value: impl Into<String>, detail: impl Into<String>) -> Self {
        Self {
            value: value.into(),
            detail: detail.into(),
        }
    }
}

#[derive(Debug, Clone)]
pub struct AutocompleteState {
    pub trigger: AutocompleteTrigger,
    pub query: String,
    pub items: Vec<AutocompleteItem>,
    pub selected: usize,
}

impl AutocompleteState {
    pub fn new(trigger: AutocompleteTrigger, query: String, items: Vec<AutocompleteItem>) -> Self {
        let mut state = Self {
            trigger,
            query,
            items,
            selected: 0,
        };
        state.filter();
        state
    }

    pub fn next(&mut self) {
        if !self.items.is_empty() {
            self.selected = (self.selected + 1) % self.items.len();
        }
    }

    pub fn prev(&mut self) {
        if !self.items.is_empty() {
            self.selected = self.selected.saturating_sub(1);
        }
    }

    pub fn current(&self) -> Option<&AutocompleteItem> {
        self.items.get(self.selected)
    }

    fn filter(&mut self) {
        if self.query.is_empty() {
            return;
        }
        let needle = self.query.to_lowercase();
        self.items
            .retain(|item| item.value.to_lowercase().contains(&needle));
    }
}
