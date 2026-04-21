use serde::Deserialize;

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SandboxApplyLandlockInput {
    pub writable_roots: Vec<String>,
    pub network: bool,
}

#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SandboxApplyLandlockOutput {
    pub applied: bool,
    pub platform: String,
    pub network_blocked: bool,
}

#[cfg(target_os = "linux")]
mod linux {
    use super::{SandboxApplyLandlockInput, SandboxApplyLandlockOutput};
    use std::ffi::CString;
    use std::os::fd::RawFd;

    #[repr(C)]
    struct LandlockRulesetAttr {
        handled_access_fs: u64,
    }

    #[repr(C)]
    struct LandlockPathBeneathAttr {
        allowed_access: u64,
        parent_fd: i32,
    }

    const ACCESS_FS_WRITE_FILE: u64 = 1 << 1;
    const ACCESS_FS_REMOVE_DIR: u64 = 1 << 4;
    const ACCESS_FS_REMOVE_FILE: u64 = 1 << 5;
    const ACCESS_FS_MAKE_CHAR: u64 = 1 << 6;
    const ACCESS_FS_MAKE_DIR: u64 = 1 << 7;
    const ACCESS_FS_MAKE_REG: u64 = 1 << 8;
    const ACCESS_FS_MAKE_SOCK: u64 = 1 << 9;
    const ACCESS_FS_MAKE_FIFO: u64 = 1 << 10;
    const ACCESS_FS_MAKE_BLOCK: u64 = 1 << 11;
    const ACCESS_FS_MAKE_SYM: u64 = 1 << 12;
    const ACCESS_FS_REFER: u64 = 1 << 13;
    const ACCESS_FS_TRUNCATE: u64 = 1 << 14;
    const ACCESS_FS_IOCTL_DEV: u64 = 1 << 15;

    const SYS_LANDLOCK_CREATE_RULESET: libc::c_long = 444;
    const SYS_LANDLOCK_ADD_RULE: libc::c_long = 445;
    const SYS_LANDLOCK_RESTRICT_SELF: libc::c_long = 446;
    const LANDLOCK_RULE_PATH_BENEATH: libc::c_int = 1;

    fn handled_access_mask() -> u64 {
        writable_access_mask()
    }

    fn writable_access_mask() -> u64 {
        ACCESS_FS_WRITE_FILE
            | ACCESS_FS_REMOVE_DIR
            | ACCESS_FS_REMOVE_FILE
            | ACCESS_FS_MAKE_CHAR
            | ACCESS_FS_MAKE_DIR
            | ACCESS_FS_MAKE_REG
            | ACCESS_FS_MAKE_SOCK
            | ACCESS_FS_MAKE_FIFO
            | ACCESS_FS_MAKE_BLOCK
            | ACCESS_FS_MAKE_SYM
            | ACCESS_FS_REFER
            | ACCESS_FS_TRUNCATE
            | ACCESS_FS_IOCTL_DEV
    }

    fn create_ruleset() -> Result<RawFd, String> {
        let attr = LandlockRulesetAttr {
            handled_access_fs: handled_access_mask(),
        };

        // SAFETY: We pass a valid pointer/size pair for `LandlockRulesetAttr`, and the
        // syscall number/arguments follow the Linux Landlock ABI.
        let fd = unsafe {
            libc::syscall(
                SYS_LANDLOCK_CREATE_RULESET,
                &attr as *const LandlockRulesetAttr,
                std::mem::size_of::<LandlockRulesetAttr>(),
                0,
            ) as i32
        };

        if fd < 0 {
            return Err(format!(
                "landlock_create_ruleset failed: {}",
                std::io::Error::last_os_error()
            ));
        }
        Ok(fd)
    }

    fn open_root(path: &str) -> Result<RawFd, String> {
        let metadata =
            std::fs::metadata(path).map_err(|_| format!("writable root does not exist: {path}"))?;
        if !metadata.is_dir() {
            return Err(format!("writable root must be a directory: {path}"));
        }

        let c_path = CString::new(path).map_err(|_| format!("invalid path: {path}"))?;
        // SAFETY: `c_path` is a valid NUL-terminated C string for the duration of the call.
        let fd = unsafe { libc::open(c_path.as_ptr(), libc::O_PATH | libc::O_CLOEXEC) };
        if fd < 0 {
            return Err(format!(
                "failed to open writable root {path}: {}",
                std::io::Error::last_os_error()
            ));
        }
        Ok(fd)
    }

    fn close_fd(fd: RawFd) {
        // SAFETY: `fd` is an owned descriptor returned by a successful syscall in this module.
        let _ = unsafe { libc::close(fd) };
    }

    fn add_path_rule(ruleset_fd: RawFd, root_fd: RawFd) -> Result<(), String> {
        let rule = LandlockPathBeneathAttr {
            allowed_access: writable_access_mask(),
            parent_fd: root_fd,
        };

        // SAFETY: We pass a valid pointer to `LandlockPathBeneathAttr` and required ABI args.
        let rc = unsafe {
            libc::syscall(
                SYS_LANDLOCK_ADD_RULE,
                ruleset_fd,
                LANDLOCK_RULE_PATH_BENEATH,
                &rule as *const LandlockPathBeneathAttr,
                0,
            ) as i32
        };

        if rc < 0 {
            return Err(format!(
                "landlock_add_rule failed: {}",
                std::io::Error::last_os_error()
            ));
        }

        Ok(())
    }

    fn restrict_self(ruleset_fd: RawFd) -> Result<(), String> {
        // SAFETY: `prctl(PR_SET_NO_NEW_PRIVS, 1, ..)` is a pointer-free call that permanently
        // disables privilege escalation for this process, which Landlock requires.
        let rc = unsafe { libc::prctl(libc::PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) };
        if rc != 0 {
            return Err(format!(
                "failed to set PR_SET_NO_NEW_PRIVS: {}",
                std::io::Error::last_os_error()
            ));
        }

        // SAFETY: The syscall uses a live Landlock ruleset fd and a zero flags argument.
        let rc = unsafe { libc::syscall(SYS_LANDLOCK_RESTRICT_SELF, ruleset_fd, 0) as i32 };
        if rc < 0 {
            return Err(format!(
                "landlock_restrict_self failed: {}",
                std::io::Error::last_os_error()
            ));
        }

        Ok(())
    }

    fn maybe_unshare_network(_network_enabled: bool) -> bool {
        false
    }

    pub fn apply(input: SandboxApplyLandlockInput) -> Result<SandboxApplyLandlockOutput, String> {
        if input.writable_roots.is_empty() {
            return Err("writable_roots must not be empty".to_string());
        }

        let ruleset_fd = create_ruleset()?;
        for root in &input.writable_roots {
            let root_fd = open_root(root)?;
            let add_rule_result = add_path_rule(ruleset_fd, root_fd);
            close_fd(root_fd);
            add_rule_result?;
        }

        if let Err(error) = restrict_self(ruleset_fd) {
            close_fd(ruleset_fd);
            return Err(error);
        }
        close_fd(ruleset_fd);

        let network_blocked = maybe_unshare_network(input.network);

        Ok(SandboxApplyLandlockOutput {
            applied: true,
            platform: "linux".to_string(),
            network_blocked,
        })
    }
}

#[tauri::command]
pub fn sandbox_apply_landlock(
    input: SandboxApplyLandlockInput,
) -> Result<SandboxApplyLandlockOutput, String> {
    #[cfg(target_os = "linux")]
    {
        return linux::apply(input);
    }

    #[cfg(not(target_os = "linux"))]
    {
        let _ = input;
        Ok(SandboxApplyLandlockOutput {
            applied: false,
            platform: std::env::consts::OS.to_string(),
            network_blocked: false,
        })
    }
}
