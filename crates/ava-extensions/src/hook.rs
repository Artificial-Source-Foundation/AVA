use std::collections::HashMap;
use std::sync::Arc;

/// Stable points where extensions can observe or modify behavior.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum HookPoint {
    /// Runs before tool execution begins.
    BeforeToolExecution,
    /// Runs after tool execution completes.
    AfterToolExecution,
}

/// Context passed to each hook invocation.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct HookContext {
    /// Logical scope for the current hook call.
    pub scope: String,
    /// Additional key/value metadata provided by the caller.
    pub metadata: HashMap<String, String>,
}

impl HookContext {
    /// Creates a hook context with an empty metadata map.
    pub fn new(scope: impl Into<String>) -> Self {
        Self {
            scope: scope.into(),
            metadata: HashMap::new(),
        }
    }
}

/// Shared callback signature used by registered hooks.
pub type HookHandler = Arc<dyn Fn(&HookContext) -> String + Send + Sync>;

/// Named hook handler bound to a specific hook point and extension.
#[derive(Clone)]
pub struct Hook {
    name: String,
    point: HookPoint,
    extension_name: String,
    handler: HookHandler,
}

impl Hook {
    /// Creates an unbound hook with an empty extension name.
    pub fn new(
        name: impl Into<String>,
        point: HookPoint,
        handler: impl Fn(&HookContext) -> String + Send + Sync + 'static,
    ) -> Self {
        Self {
            name: name.into(),
            point,
            extension_name: String::new(),
            handler: Arc::new(handler),
        }
    }

    /// Associates this hook with an extension name.
    pub fn bind_extension(mut self, extension_name: impl Into<String>) -> Self {
        self.extension_name = extension_name.into();
        self
    }

    /// Returns the hook identifier.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Returns where in execution this hook runs.
    pub fn point(&self) -> &HookPoint {
        &self.point
    }

    /// Returns the owning extension name.
    pub fn extension_name(&self) -> &str {
        &self.extension_name
    }

    /// Executes the hook against the provided context.
    pub fn invoke(&self, context: &HookContext) -> String {
        (self.handler)(context)
    }
}

/// Registry grouped by hook point for fast invocation.
#[derive(Default)]
pub struct HookRegistry {
    hooks_by_point: HashMap<HookPoint, Vec<Hook>>,
}

impl HookRegistry {
    /// Creates an empty hook registry.
    pub fn new() -> Self {
        Self::default()
    }

    /// Replaces all hooks currently registered by the extension.
    pub fn replace_extension(&mut self, extension_name: &str, hooks: &[Hook]) {
        self.remove_extension(extension_name);
        for hook in hooks {
            let bound = hook.clone().bind_extension(extension_name.to_owned());
            self.hooks_by_point
                .entry(bound.point.clone())
                .or_default()
                .push(bound);
        }
    }

    /// Removes all hooks owned by the extension.
    pub fn remove_extension(&mut self, extension_name: &str) {
        for hooks in self.hooks_by_point.values_mut() {
            hooks.retain(|hook| hook.extension_name() != extension_name);
        }
    }

    /// Invokes all hooks for a point and returns their outputs in registration order.
    pub fn invoke(&self, point: HookPoint, context: &HookContext) -> Vec<String> {
        self.hooks_by_point
            .get(&point)
            .map(|hooks| hooks.iter().map(|hook| hook.invoke(context)).collect())
            .unwrap_or_default()
    }
}
