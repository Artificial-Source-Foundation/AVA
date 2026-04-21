use std::path::Path;

use libloading::Library;

use crate::manager::{Extension, ExtensionError};

type ExtensionFactory = unsafe fn() -> *mut dyn Extension;

/// The trusted directory for native extensions. Only extensions located under
/// `$XDG_CONFIG_HOME/ava/extensions/trusted/` are permitted to load.
const TRUSTED_SUBDIR: &str = "extensions/trusted";

/// Check whether an extension path is inside the trusted extensions directory.
///
/// Returns `Ok(())` if the path is trusted, or `Err(ExtensionError)` if not.
fn verify_trusted_path(path: &Path) -> Result<(), ExtensionError> {
    let trusted_dir = dirs_next::config_dir()
        .map(|dir| dir.join("ava").join(TRUSTED_SUBDIR))
        .ok_or_else(|| {
            ExtensionError::LoadFailure(
                "cannot determine config directory for trust check".to_string(),
            )
        })?;

    // Canonicalize both paths to prevent symlink/traversal escapes.
    // If the trusted directory doesn't exist yet, that means no extensions
    // are trusted — fail closed.
    let canonical_trusted = std::fs::canonicalize(&trusted_dir).map_err(|_| {
        ExtensionError::LoadFailure(format!(
            "trusted extensions directory does not exist: {}. \
             Create it and place approved extensions there.",
            trusted_dir.display()
        ))
    })?;

    let canonical_path = std::fs::canonicalize(path).map_err(|e| {
        ExtensionError::LoadFailure(format!(
            "cannot canonicalize extension path {}: {e}",
            path.display()
        ))
    })?;

    if !canonical_path.starts_with(&canonical_trusted) {
        return Err(ExtensionError::LoadFailure(format!(
            "extension {} is not in the trusted directory ({}). \
             Move it there to allow loading.",
            path.display(),
            trusted_dir.display()
        )));
    }

    Ok(())
}

/// Loads a native Rust extension from a shared library.
///
/// # Safety
/// - The extension path must be inside `$XDG_CONFIG_HOME/ava/extensions/trusted/`.
/// - The extension binary must be built against a compatible Rust toolchain and
///   the same `ava-extensions` trait definitions as the host process.
/// - The `ava_extension_create` symbol must return a valid, non-null pointer
///   allocated with an allocator compatible with the host.
/// - The returned extension object must remain valid for as long as the loaded
///   library stays resident in memory.
pub unsafe fn load_native_extension(path: &Path) -> Result<Box<dyn Extension>, ExtensionError> {
    if !path.exists() {
        return Err(ExtensionError::FileNotFound(path.to_path_buf()));
    }

    // SEC-3: Verify the extension is in the trusted directory before loading.
    verify_trusted_path(path)?;

    tracing::warn!(
        path = %path.display(),
        "Loading native extension — ensure this binary is from a trusted source"
    );

    // SAFETY: `path` has been verified to exist and is in the trusted directory;
    // `Library::new` loads the shared object and resolves its symbols.
    // Caller guarantees ABI compatibility.
    let library = unsafe { Library::new(path) }
        .map_err(|error| ExtensionError::LoadFailure(error.to_string()))?;

    let constructor = load_constructor(&library)?;
    // SAFETY: `constructor` was resolved from a valid library symbol; caller
    // guarantees the factory returns a valid, heap-allocated `dyn Extension`.
    let raw_extension = unsafe { constructor() };
    if raw_extension.is_null() {
        return Err(ExtensionError::LoadFailure(
            "ava_extension_create returned a null pointer".to_string(),
        ));
    }

    // SAFETY: `raw_extension` is non-null (checked above) and was allocated by
    // the extension factory with the global allocator.
    let extension = unsafe { Box::from_raw(raw_extension) };

    // WHY: The library must stay loaded so the extension vtable (trait object
    // function pointers) remains valid for the lifetime of the `Box<dyn Extension>`.
    // Dropping `Library` would unload the shared object, leaving dangling vtable
    // pointers and causing UB on any subsequent method call.
    //
    // LIMITATION: Extensions loaded this way cannot be unloaded at runtime.
    // Once loaded, the shared library remains resident until process exit.
    //
    // FUTURE: Store an `Arc<Library>` alongside the extension (e.g. in a wrapper
    // struct) so the library's lifetime is tied to the extension object. This
    // would enable proper lifecycle management and eventual hot-reload support.
    tracing::warn!(
        path = %path.display(),
        "Using std::mem::forget(library) to keep native extension loaded — \
         proper lifecycle management (Arc<Library>) not yet implemented"
    );
    std::mem::forget(library);

    Ok(extension)
}

fn load_constructor<'lib>(
    library: &'lib Library,
) -> Result<libloading::Symbol<'lib, ExtensionFactory>, ExtensionError> {
    // SAFETY: `library` is a valid loaded shared object; we look up a known
    // symbol name that must conform to the `ExtensionFactory` signature.
    unsafe { library.get::<ExtensionFactory>(b"ava_extension_create\0") }
        .map_err(|_| ExtensionError::MissingSymbol("ava_extension_create".to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_missing_file_error() {
        // SAFETY: the path does not exist, so `Library::new` will fail with an IO error
        // before any foreign code is loaded. No actual shared library is opened.
        let result =
            unsafe { load_native_extension(Path::new("/path/that/does/not/exist/libmissing.so")) };
        assert!(matches!(result, Err(ExtensionError::FileNotFound(_))));
    }

    #[test]
    fn test_untrusted_path_rejected() {
        // Create a temp file outside the trusted directory
        let dir = tempfile::tempdir().unwrap();
        let fake_ext = dir.path().join("libevil.so");
        std::fs::write(&fake_ext, b"not a real library").unwrap();

        // SAFETY: the trust check should reject before loading
        let result = unsafe { load_native_extension(&fake_ext) };
        let err = match result {
            Err(e @ ExtensionError::LoadFailure(_)) => e,
            Err(other) => panic!("expected LoadFailure, got: {other:?}"),
            Ok(_) => panic!("expected error, got Ok"),
        };
        let err_msg = format!("{err:?}");
        assert!(
            err_msg.contains("trusted"),
            "error should mention trusted directory: {err_msg}"
        );
    }

    #[cfg(unix)]
    #[test]
    fn test_missing_symbol_error() {
        let current_process = libloading::os::unix::Library::this();
        let library: Library = current_process.into();

        let result = load_constructor(&library);

        assert!(matches!(result, Err(ExtensionError::MissingSymbol(_))));
    }
}
