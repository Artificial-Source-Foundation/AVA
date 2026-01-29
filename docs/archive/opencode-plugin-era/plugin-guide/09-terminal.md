# Terminal & Process Management

## Cross-Platform Terminal Spawning

Pattern from `worktree`:

```typescript
type TerminalType = "macos" | "linux-desktop" | "windows" | "tmux";

function detectTerminalType(): TerminalType {
  // Check tmux first (works everywhere)
  if (process.env.TMUX) return "tmux";

  // Platform-specific
  switch (process.platform) {
    case "darwin": return "macos";
    case "win32": return "windows";
    case "linux": return "linux-desktop";
    default: return "linux-desktop";
  }
}

async function openTerminal(options: {
  cwd: string;
  command?: string;
  title?: string;
}): Promise<{ success: boolean; error?: string }> {
  const type = detectTerminalType();

  switch (type) {
    case "tmux":
      return openTmuxWindow(options);
    case "macos":
      return openMacOSTerminal(options);
    case "linux-desktop":
      return openLinuxTerminal(options);
    case "windows":
      return openWindowsTerminal(options);
  }
}
```

---

## Tmux with Mutex Protection

```typescript
class Mutex {
  private locked = false;
  private queue: Array<() => void> = [];

  async acquire(): Promise<void> {
    if (!this.locked) {
      this.locked = true;
      return;
    }

    return new Promise<void>(resolve => {
      this.queue.push(resolve);
    });
  }

  release(): void {
    const next = this.queue.shift();
    if (next) {
      next();
    } else {
      this.locked = false;
    }
  }

  async runExclusive<T>(fn: () => Promise<T>): Promise<T> {
    await this.acquire();
    try {
      return await fn();
    } finally {
      this.release();
    }
  }
}

const tmuxMutex = new Mutex();
const STABILIZATION_DELAY = 100;

async function openTmuxWindow(options: {
  cwd: string;
  command?: string;
  windowName?: string;
}): Promise<{ success: boolean }> {
  return tmuxMutex.runExclusive(async () => {
    const args = [
      "new-window",
      "-n", options.windowName || "opencode",
      "-c", options.cwd,
    ];

    if (options.command) {
      args.push(options.command);
    }

    const proc = Bun.spawnSync(["tmux", ...args]);

    // Stabilization delay prevents timing races
    await Bun.sleep(STABILIZATION_DELAY);

    return { success: proc.exitCode === 0 };
  });
}
```

---

## Safe Command Escaping

```typescript
function escapeBash(str: string): string {
  // Order matters!
  return str
    .replace(/\\/g, "\\\\")   // Backslash first
    .replace(/"/g, '\\"')     // Double quotes
    .replace(/\$/g, "\\$")    // Variable expansion
    .replace(/`/g, "\\`")     // Command substitution
    .replace(/!/g, "\\!")     // History expansion
    .replace(/\n/g, " ");     // Newlines to spaces
}

function escapeBatch(str: string): string {
  return str
    .replace(/%/g, "%%")
    .replace(/\^/g, "^^")
    .replace(/&/g, "^&")
    .replace(/</g, "^<")
    .replace(/>/g, "^>");
}

// Self-cleaning temp script
function wrapWithSelfCleanup(script: string): string {
  return `#!/bin/bash
trap 'rm -f "$0"' EXIT INT TERM
${script}`;
}
```

---

## IPC via Unix Domain Sockets

Pattern from `canvas`:

```typescript
async function sendIpcMessage(socketPath: string, message: object): Promise<object | null> {
  return new Promise((resolve) => {
    let resolved = false;
    const timeout = setTimeout(() => {
      if (!resolved) {
        resolved = true;
        resolve(null);
      }
    }, 2000);

    Bun.connect({
      unix: socketPath,
      socket: {
        data(_socket, data) {
          if (resolved) return;
          clearTimeout(timeout);
          resolved = true;
          try {
            resolve(JSON.parse(data.toString().trim()));
          } catch {
            resolve(null);
          }
          _socket.end();
        },
        open(socket) {
          socket.write(JSON.stringify(message) + "\n");
        },
        close() {
          if (!resolved) {
            resolved = true;
            clearTimeout(timeout);
            resolve(null);
          }
        },
        error() {
          if (!resolved) {
            resolved = true;
            clearTimeout(timeout);
            resolve(null);
          }
        },
      },
    }).catch(() => {
      if (!resolved) {
        resolved = true;
        clearTimeout(timeout);
        resolve(null);
      }
    });
  });
}
```

---

## Platform-Specific Terminal Commands

### macOS (AppleScript)

```typescript
async function openMacOSTerminal(options: { cwd: string; command?: string }) {
  const script = `
    tell application "Terminal"
      activate
      do script "cd ${escapeBash(options.cwd)}${options.command ? ` && ${escapeBash(options.command)}` : ''}"
    end tell
  `;

  const proc = Bun.spawnSync(["osascript", "-e", script]);
  return { success: proc.exitCode === 0 };
}
```

### Linux (xdg-open or direct)

```typescript
async function openLinuxTerminal(options: { cwd: string; command?: string }) {
  // Try common terminals
  const terminals = ["gnome-terminal", "konsole", "xfce4-terminal", "xterm"];

  for (const term of terminals) {
    try {
      const args = term === "gnome-terminal"
        ? ["--working-directory", options.cwd]
        : ["-e", `cd ${options.cwd} && $SHELL`];

      Bun.spawn([term, ...args], { detached: true });
      return { success: true };
    } catch {
      continue;
    }
  }

  return { success: false, error: "No terminal found" };
}
```

### Windows (PowerShell)

```typescript
async function openWindowsTerminal(options: { cwd: string; command?: string }) {
  const proc = Bun.spawnSync([
    "powershell",
    "-Command",
    `Start-Process powershell -WorkingDirectory "${options.cwd}"`,
  ]);
  return { success: proc.exitCode === 0 };
}
```

---

## Source Reference

- `worktree/src/lib/spawn/` - Cross-platform spawning
- `canvas/src/index.ts` - IPC sockets
- `pilot/src/` - Terminal automation
