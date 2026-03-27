use std::collections::HashMap;
use std::path::PathBuf;

use ava_extensions::{
    Extension, ExtensionDescriptor, ExtensionManager, Hook, HookContext, HookPoint,
    NativeExtensionDescriptor, WasmExtensionDescriptor,
};

#[derive(Clone)]
struct TestExtension {
    descriptor: NativeExtensionDescriptor,
}

impl Extension for TestExtension {
    fn descriptor(&self) -> NativeExtensionDescriptor {
        self.descriptor.clone()
    }
}

fn native_descriptor(name: &str, hook_marker: &str) -> NativeExtensionDescriptor {
    let marker = hook_marker.to_string();
    NativeExtensionDescriptor {
        name: name.to_string(),
        version: "0.1.0".to_string(),
        path: PathBuf::from(format!("/tmp/{name}.so")),
        tools: vec!["tool.alpha".to_string(), "tool.beta".to_string()],
        hooks: vec![Hook::new(
            "before-tool",
            HookPoint::BeforeToolExecution,
            move |_| marker.clone(),
        )],
        validators: vec!["validator.schema".to_string()],
    }
}

#[test]
fn register_native_collects_tools_hooks_and_validators() {
    let mut manager = ExtensionManager::new();
    let extension = TestExtension {
        descriptor: native_descriptor("native-ext", "native-hook"),
    };

    manager
        .register_native(extension)
        .expect("native registration");

    let descriptor = manager
        .get_descriptor("native-ext")
        .expect("descriptor should exist");

    match descriptor {
        ExtensionDescriptor::Native(native) => {
            assert_eq!(native.tools, vec!["tool.alpha", "tool.beta"]);
            assert_eq!(native.validators, vec!["validator.schema"]);
            assert_eq!(native.hooks.len(), 1);
        }
        ExtensionDescriptor::Wasm(_) => panic!("expected native descriptor"),
    }

    let outputs = manager.invoke_hooks(
        HookPoint::BeforeToolExecution,
        &HookContext::new("native-ext"),
    );
    assert_eq!(outputs, vec!["native-hook"]);
}

#[test]
fn register_wasm_module_stores_descriptor_metadata() {
    let mut manager = ExtensionManager::new();
    let metadata = HashMap::from([(String::from("runtime"), String::from("wasm32-wasi"))]);

    manager
        .register_wasm_module(WasmExtensionDescriptor {
            name: "wasm-ext".to_string(),
            version: "1.4.2".to_string(),
            path: PathBuf::from("/tmp/wasm-ext.wasm"),
            tools: vec!["tool.inspect".to_string()],
            hooks: vec![HookPoint::AfterToolExecution],
            validators: vec!["validator.wasm".to_string()],
            metadata: metadata.clone(),
        })
        .expect("wasm module registration");

    let descriptor = manager
        .get_descriptor("wasm-ext")
        .expect("descriptor should exist");

    match descriptor {
        ExtensionDescriptor::Wasm(wasm) => {
            assert_eq!(wasm.path, PathBuf::from("/tmp/wasm-ext.wasm"));
            assert_eq!(wasm.metadata, metadata);
            assert_eq!(wasm.hooks, vec![HookPoint::AfterToolExecution]);
        }
        ExtensionDescriptor::Native(_) => panic!("expected wasm descriptor"),
    }
}

#[test]
fn invoke_hooks_routes_by_hook_point() {
    let mut manager = ExtensionManager::new();
    let extension = TestExtension {
        descriptor: NativeExtensionDescriptor {
            name: "routing-ext".to_string(),
            version: "0.2.0".to_string(),
            path: PathBuf::from("/tmp/routing-ext.so"),
            tools: vec![],
            hooks: vec![
                Hook::new("before", HookPoint::BeforeToolExecution, |_| {
                    "before".to_string()
                }),
                Hook::new("after", HookPoint::AfterToolExecution, |_| {
                    "after".to_string()
                }),
            ],
            validators: vec![],
        },
    };

    manager
        .register_native(extension)
        .expect("native registration");

    let before = manager.invoke_hooks(
        HookPoint::BeforeToolExecution,
        &HookContext::new("routing-ext"),
    );
    let after = manager.invoke_hooks(
        HookPoint::AfterToolExecution,
        &HookContext::new("routing-ext"),
    );

    assert_eq!(before, vec!["before"]);
    assert_eq!(after, vec!["after"]);
}

#[test]
fn hot_reload_replaces_existing_extension_descriptor_by_name() {
    let mut manager = ExtensionManager::new();

    manager
        .register_native(TestExtension {
            descriptor: native_descriptor("reloadable", "old-hook"),
        })
        .expect("initial registration");

    let replacement = NativeExtensionDescriptor {
        name: "reloadable".to_string(),
        version: "0.2.0".to_string(),
        path: PathBuf::from("/tmp/reloadable-v2.so"),
        tools: vec!["tool.gamma".to_string()],
        hooks: vec![Hook::new(
            "before-tool",
            HookPoint::BeforeToolExecution,
            |_| "new-hook".to_string(),
        )],
        validators: vec!["validator.updated".to_string()],
    };

    manager
        .register_native(TestExtension {
            descriptor: replacement,
        })
        .expect("replacement registration");

    let descriptor = manager
        .get_descriptor("reloadable")
        .expect("descriptor should exist");

    match descriptor {
        ExtensionDescriptor::Native(native) => {
            assert_eq!(native.version, "0.2.0");
            assert_eq!(native.path, PathBuf::from("/tmp/reloadable-v2.so"));
            assert_eq!(native.tools, vec!["tool.gamma"]);
            assert_eq!(native.validators, vec!["validator.updated"]);
        }
        ExtensionDescriptor::Wasm(_) => panic!("expected native descriptor"),
    }

    let outputs = manager.invoke_hooks(
        HookPoint::BeforeToolExecution,
        &HookContext::new("reloadable"),
    );
    assert_eq!(outputs, vec!["new-hook"]);
}

#[test]
fn repeated_hot_reload_keeps_descriptor_and_hook_output_deterministic() {
    let mut manager = ExtensionManager::new();

    for cycle in 0..8 {
        let marker = format!("hook-{cycle}");
        manager
            .register_native(TestExtension {
                descriptor: native_descriptor("deterministic", &marker),
            })
            .expect("reload registration");

        let descriptor = manager
            .get_descriptor("deterministic")
            .expect("descriptor should exist");
        match descriptor {
            ExtensionDescriptor::Native(native) => {
                assert_eq!(native.path, PathBuf::from("/tmp/deterministic.so"));
            }
            ExtensionDescriptor::Wasm(_) => panic!("expected native descriptor"),
        }

        let outputs = manager.invoke_hooks(
            HookPoint::BeforeToolExecution,
            &HookContext::new("deterministic"),
        );
        assert_eq!(outputs, vec![marker]);
    }
}
