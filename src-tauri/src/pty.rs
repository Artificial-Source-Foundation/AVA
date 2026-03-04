use portable_pty::{native_pty_system, CommandBuilder, PtySize};
use std::collections::HashMap;
use std::io::{Read, Write};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter};

/// A single PTY session: child process + writer handle.
struct PtySession {
    writer: Box<dyn Write + Send>,
    child: Box<dyn portable_pty::Child + Send + Sync>,
}

/// Manages all active PTY sessions.
pub struct PtyManager {
    sessions: Arc<Mutex<HashMap<String, PtySession>>>,
}

impl PtyManager {
    pub fn new() -> Self {
        Self {
            sessions: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Spawn a new PTY with the user's default shell.
    pub fn spawn(
        &self,
        id: String,
        cols: u16,
        rows: u16,
        cwd: Option<String>,
        app: AppHandle,
    ) -> Result<(), String> {
        let pty_system = native_pty_system();

        let pair = pty_system
            .openpty(PtySize {
                rows,
                cols,
                pixel_width: 0,
                pixel_height: 0,
            })
            .map_err(|e| format!("Failed to open PTY: {e}"))?;

        let shell = std::env::var("SHELL").unwrap_or_else(|_| "/bin/sh".to_string());
        let mut cmd = CommandBuilder::new(&shell);
        cmd.arg("-l"); // login shell

        if let Some(ref dir) = cwd {
            cmd.cwd(dir);
        }

        let child = pair
            .slave
            .spawn_command(cmd)
            .map_err(|e| format!("Failed to spawn shell: {e}"))?;

        let writer = pair
            .master
            .take_writer()
            .map_err(|e| format!("Failed to get PTY writer: {e}"))?;

        let mut reader = pair
            .master
            .try_clone_reader()
            .map_err(|e| format!("Failed to clone PTY reader: {e}"))?;

        // Store the session
        {
            let mut sessions = self.sessions.lock().unwrap();
            sessions.insert(id.clone(), PtySession { writer, child });
        }

        // Background reader thread: emit output events
        let output_event = format!("pty-output-{id}");
        let exit_event = format!("pty-exit-{id}");
        let sessions_ref = self.sessions.clone();
        let id_clone = id.clone();

        std::thread::spawn(move || {
            let mut buf = [0u8; 4096];
            loop {
                match reader.read(&mut buf) {
                    Ok(0) => break, // EOF
                    Ok(n) => {
                        let text = String::from_utf8_lossy(&buf[..n]).to_string();
                        let _ = app.emit(&output_event, text);
                    }
                    Err(_) => break,
                }
            }

            // Get exit code if possible
            let code = {
                let mut sessions = sessions_ref.lock().unwrap();
                if let Some(session) = sessions.get_mut(&id_clone) {
                    session.child.wait().ok().map(|s| s.exit_code() as i32)
                } else {
                    None
                }
            };

            let _ = app.emit(&exit_event, code.unwrap_or(-1));

            // Clean up
            let mut sessions = sessions_ref.lock().unwrap();
            sessions.remove(&id_clone);
        });

        Ok(())
    }

    /// Write data (keystrokes) to a PTY session.
    pub fn write(&self, id: &str, data: &str) -> Result<(), String> {
        let mut sessions = self.sessions.lock().unwrap();
        let session = sessions
            .get_mut(id)
            .ok_or_else(|| format!("No PTY session: {id}"))?;
        session
            .writer
            .write_all(data.as_bytes())
            .map_err(|e| format!("Write failed: {e}"))?;
        session
            .writer
            .flush()
            .map_err(|e| format!("Flush failed: {e}"))
    }

    /// Resize a PTY session.
    pub fn resize(&self, id: &str, cols: u16, rows: u16) -> Result<(), String> {
        // portable-pty resize is on the master pair, which we don't store directly.
        // For now, this is a no-op placeholder. Full resize requires storing the master.
        let _ = (id, cols, rows);
        Ok(())
    }

    /// Kill a PTY session.
    pub fn kill(&self, id: &str) -> Result<(), String> {
        let mut sessions = self.sessions.lock().unwrap();
        if let Some(mut session) = sessions.remove(id) {
            session
                .child
                .kill()
                .map_err(|e| format!("Kill failed: {e}"))?;
        }
        Ok(())
    }
}
