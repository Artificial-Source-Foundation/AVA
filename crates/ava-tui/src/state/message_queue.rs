use ava_types::MessageTier;

/// A pending queued message shown in the composer queue display.
#[derive(Debug, Clone)]
pub struct QueuedDisplayItem {
    pub text: String,
    pub tier: MessageTier,
}

/// UI-side queue display state for mid-stream messages.
#[derive(Debug, Default, Clone)]
pub struct MessageQueueDisplay {
    pub items: Vec<QueuedDisplayItem>,
}

impl MessageQueueDisplay {
    pub fn push(&mut self, text: String, tier: MessageTier) {
        self.items.push(QueuedDisplayItem { text, tier });
    }

    pub fn clear(&mut self) {
        self.items.clear();
    }

    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    pub fn total_count(&self) -> usize {
        self.items.len()
    }

    /// Remove steering items (cleared on delivery or hard abort).
    pub fn clear_steering(&mut self) {
        self.items
            .retain(|i| !matches!(i.tier, MessageTier::Steering));
    }
}
