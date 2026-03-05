use std::path::Path;

use libloading::Library;

use crate::manager::{Extension, ExtensionError};

type ExtensionFactory = unsafe fn() -> *mut dyn Extension;

/// Loads a native Rust extension from a shared library.
///
/// # Safety
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

    let library = unsafe { Library::new(path) }
        .map_err(|error| ExtensionError::LoadFailure(error.to_string()))?;

    let constructor = load_constructor(&library)?;
    let raw_extension = unsafe { constructor() };
    if raw_extension.is_null() {
        return Err(ExtensionError::LoadFailure(
            "ava_extension_create returned a null pointer".to_string(),
        ));
    }

    let extension = unsafe { Box::from_raw(raw_extension) };

    // Keep the library loaded so the extension vtable remains valid.
    std::mem::forget(library);

    Ok(extension)
}

fn load_constructor<'lib>(
    library: &'lib Library,
) -> Result<libloading::Symbol<'lib, ExtensionFactory>, ExtensionError> {
    unsafe { library.get::<ExtensionFactory>(b"ava_extension_create\0") }
        .map_err(|_| ExtensionError::MissingSymbol("ava_extension_create".to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_missing_file_error() {
        let result =
            unsafe { load_native_extension(Path::new("/path/that/does/not/exist/libmissing.so")) };
        assert!(matches!(result, Err(ExtensionError::FileNotFound(_))));
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
