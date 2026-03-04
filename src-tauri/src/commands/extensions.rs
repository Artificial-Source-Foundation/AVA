use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::PathBuf;

use ava_extensions::{
    Extension, ExtensionDescriptor, ExtensionManager, Hook, HookPoint, NativeExtensionDescriptor,
    WasmExtensionDescriptor,
};

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HookPointInput {
    BeforeToolExecution,
    AfterToolExecution,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct NativeHookInput {
    pub name: String,
    pub point: HookPointInput,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RegisterNativeExtensionInput {
    pub name: String,
    pub version: String,
    pub path: String,
    pub tools: Vec<String>,
    pub hooks: Vec<NativeHookInput>,
    pub validators: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RegisterWasmExtensionInput {
    pub name: String,
    pub version: String,
    pub path: String,
    pub tools: Vec<String>,
    pub hooks: Vec<HookPointInput>,
    pub validators: Vec<String>,
    pub metadata: HashMap<String, String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum HookPointOutput {
    BeforeToolExecution,
    AfterToolExecution,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HookOutput {
    pub name: String,
    pub point: HookPointOutput,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NativeDescriptorOutput {
    pub name: String,
    pub version: String,
    pub path: String,
    pub tools: Vec<String>,
    pub hooks: Vec<HookOutput>,
    pub validators: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct WasmDescriptorOutput {
    pub name: String,
    pub version: String,
    pub path: String,
    pub tools: Vec<String>,
    pub hooks: Vec<HookPointOutput>,
    pub validators: Vec<String>,
    pub metadata: HashMap<String, String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ExtensionRegistrationOutput {
    Native(NativeDescriptorOutput),
    Wasm(WasmDescriptorOutput),
}

impl From<HookPointInput> for HookPoint {
    fn from(value: HookPointInput) -> Self {
        match value {
            HookPointInput::BeforeToolExecution => HookPoint::BeforeToolExecution,
            HookPointInput::AfterToolExecution => HookPoint::AfterToolExecution,
        }
    }
}

impl From<HookPoint> for HookPointOutput {
    fn from(value: HookPoint) -> Self {
        match value {
            HookPoint::BeforeToolExecution => HookPointOutput::BeforeToolExecution,
            HookPoint::AfterToolExecution => HookPointOutput::AfterToolExecution,
        }
    }
}

impl From<&HookPoint> for HookPointOutput {
    fn from(value: &HookPoint) -> Self {
        match value {
            HookPoint::BeforeToolExecution => HookPointOutput::BeforeToolExecution,
            HookPoint::AfterToolExecution => HookPointOutput::AfterToolExecution,
        }
    }
}

impl From<&ExtensionDescriptor> for ExtensionRegistrationOutput {
    fn from(value: &ExtensionDescriptor) -> Self {
        match value {
            ExtensionDescriptor::Native(native) => {
                let hooks = native
                    .hooks
                    .iter()
                    .map(|hook| HookOutput {
                        name: hook.name().to_string(),
                        point: hook.point().into(),
                    })
                    .collect();

                ExtensionRegistrationOutput::Native(NativeDescriptorOutput {
                    name: native.name.clone(),
                    version: native.version.clone(),
                    path: native.path.to_string_lossy().to_string(),
                    tools: native.tools.clone(),
                    hooks,
                    validators: native.validators.clone(),
                })
            }
            ExtensionDescriptor::Wasm(wasm) => {
                ExtensionRegistrationOutput::Wasm(WasmDescriptorOutput {
                    name: wasm.name.clone(),
                    version: wasm.version.clone(),
                    path: wasm.path.to_string_lossy().to_string(),
                    tools: wasm.tools.clone(),
                    hooks: wasm.hooks.iter().map(HookPointOutput::from).collect(),
                    validators: wasm.validators.clone(),
                    metadata: wasm.metadata.clone(),
                })
            }
        }
    }
}

#[derive(Clone)]
struct NativeExtensionAdapter {
    descriptor: NativeExtensionDescriptor,
}

impl Extension for NativeExtensionAdapter {
    fn descriptor(&self) -> NativeExtensionDescriptor {
        self.descriptor.clone()
    }
}

#[tauri::command]
pub fn extensions_register_native(
    input: RegisterNativeExtensionInput,
) -> Result<ExtensionRegistrationOutput, String> {
    let RegisterNativeExtensionInput {
        name,
        version,
        path,
        tools,
        hooks,
        validators,
    } = input;

    let mut manager = ExtensionManager::new();
    let extension_name = name.clone();

    let native_hooks = hooks
        .into_iter()
        .map(|hook| {
            let hook_name = hook.name;
            let extension_name_for_handler = extension_name.clone();
            let hook_name_for_handler = hook_name.clone();
            Hook::new(hook_name, hook.point.into(), move |_| {
                format!("{extension_name_for_handler}:{hook_name_for_handler}")
            })
        })
        .collect();

    let descriptor = NativeExtensionDescriptor {
        name: name.clone(),
        version,
        path: PathBuf::from(path),
        tools,
        hooks: native_hooks,
        validators,
    };

    manager
        .register_native(NativeExtensionAdapter { descriptor })
        .map_err(|error| error.to_string())?;

    manager
        .get_descriptor(&name)
        .map(ExtensionRegistrationOutput::from)
        .ok_or_else(|| "registered extension descriptor was not found".to_string())
}

#[tauri::command]
pub fn extensions_register_wasm(
    input: RegisterWasmExtensionInput,
) -> Result<ExtensionRegistrationOutput, String> {
    let mut manager = ExtensionManager::new();

    let descriptor = WasmExtensionDescriptor {
        name: input.name.clone(),
        version: input.version,
        path: PathBuf::from(input.path),
        tools: input.tools,
        hooks: input.hooks.into_iter().map(HookPoint::from).collect(),
        validators: input.validators,
        metadata: input.metadata,
    };

    manager
        .register_wasm_module(descriptor)
        .map_err(|error| error.to_string())?;

    manager
        .get_descriptor(&input.name)
        .map(ExtensionRegistrationOutput::from)
        .ok_or_else(|| "registered extension descriptor was not found".to_string())
}

#[cfg(test)]
mod tests {
    use super::{extensions_register_native, extensions_register_wasm};
    use super::{RegisterNativeExtensionInput, RegisterWasmExtensionInput};
    use serde_json::json;

    #[test]
    fn native_registration_maps_dto_and_serializes_output() {
        let input: RegisterNativeExtensionInput = serde_json::from_value(json!({
            "name": "native-ext",
            "version": "1.2.3",
            "path": "/tmp/native-ext.so",
            "tools": ["tool.alpha"],
            "hooks": [{
                "name": "after-hook",
                "point": "after_tool_execution"
            }],
            "validators": ["validator.schema"]
        }))
        .expect("native input should deserialize");

        let output = extensions_register_native(input).expect("native registration should succeed");
        let output_json = serde_json::to_value(&output).expect("native output should serialize");

        assert_eq!(output_json["kind"], "native");
        assert_eq!(output_json["name"], "native-ext");
        assert_eq!(output_json["hooks"][0]["point"], "after_tool_execution");
    }

    #[test]
    fn wasm_registration_maps_dto_and_serializes_output() {
        let input: RegisterWasmExtensionInput = serde_json::from_value(json!({
            "name": "wasm-ext",
            "version": "0.9.0",
            "path": "/tmp/wasm-ext.wasm",
            "tools": ["tool.beta"],
            "hooks": ["before_tool_execution"],
            "validators": ["validator.wasm"],
            "metadata": {
                "runtime": "wasm32-wasi"
            }
        }))
        .expect("wasm input should deserialize");

        let output = extensions_register_wasm(input).expect("wasm registration should succeed");
        let output_json = serde_json::to_value(&output).expect("wasm output should serialize");

        assert_eq!(output_json["kind"], "wasm");
        assert_eq!(output_json["name"], "wasm-ext");
        assert_eq!(output_json["metadata"]["runtime"], "wasm32-wasi");
    }
}
