# Safety & Protection

## Command Interception (PreToolUse)

Pattern from `safety-net`:

```typescript
interface HookInput {
  tool_name: string;
  tool_input: { command?: string };
  cwd: string;
}

interface HookOutput {
  hookSpecificOutput: {
    hookEventName: "PreToolUse";
    permissionDecision: "allow" | "deny";
    permissionDecisionReason?: string;
  };
}

// Dangerous patterns to block
const DANGEROUS_PATTERNS = {
  git: [
    { pattern: /checkout\s+--\s+/, reason: "discards uncommitted changes" },
    { pattern: /reset\s+--hard/, reason: "destroys uncommitted work" },
    { pattern: /clean\s+-f/, reason: "removes untracked files" },
    { pattern: /push\s+--force(?!-with-lease)/, reason: "destroys remote history" },
  ],
  rm: [
    { pattern: /rm\s+-rf\s+\/(?!tmp)/, reason: "destructive path deletion" },
    { pattern: /rm\s+-rf\s+~/, reason: "home directory deletion" },
  ],
};

function analyzeCommand(command: string, cwd: string): string | null {
  // Check git commands
  if (command.startsWith("git ")) {
    for (const { pattern, reason } of DANGEROUS_PATTERNS.git) {
      if (pattern.test(command)) {
        return `git ${reason}`;
      }
    }
  }

  // Check rm commands
  if (/\brm\s/.test(command)) {
    // Allow within cwd and temp directories
    if (isWithinCwd(command, cwd) || isTemporaryPath(command)) {
      return null;
    }
    for (const { pattern, reason } of DANGEROUS_PATTERNS.rm) {
      if (pattern.test(command)) {
        return reason;
      }
    }
  }

  return null; // Allow
}

function outputDeny(reason: string): void {
  const output: HookOutput = {
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: "deny",
      permissionDecisionReason: `BLOCKED by Safety Net: ${reason}`,
    },
  };
  console.log(JSON.stringify(output));
}
```

---

## Path Validation

```typescript
function isPathSafe(basePath: string, requestedPath: string): boolean {
  // Reject absolute paths
  if (path.isAbsolute(requestedPath)) return false;

  // Reject obvious traversal
  if (requestedPath.includes("..")) return false;

  // Verify resolved path stays within base
  const resolved = path.resolve(basePath, requestedPath);
  const normalizedBase = path.resolve(basePath) + path.sep;

  return resolved.startsWith(normalizedBase) || resolved === path.resolve(basePath);
}

function validatePath(basePath: string, requestedPath: string): string {
  if (!isPathSafe(basePath, requestedPath)) {
    throw new Error(`Path "${requestedPath}" is outside allowed directory`);
  }
  return path.resolve(basePath, requestedPath);
}
```

---

## Sensitive File Protection

Pattern from `envsitter-guard`:

```typescript
const SENSITIVE_PATTERNS = [
  /^\.env$/,
  /^\.env\./,
  /\.pem$/,
  /\.key$/,
  /credentials\./,
  /secrets?\./i,
];

function isSensitivePath(filePath: string): boolean {
  const filename = path.basename(filePath);
  return SENSITIVE_PATTERNS.some(pattern => pattern.test(filename));
}

// In tool.execute.before hook
"tool.execute.before": async (input, output) => {
  const filePath = output.args?.filePath || output.args?.file_path;
  if (!filePath) return;

  if (isSensitivePath(filePath)) {
    if (input.tool === "Read" || input.tool === "read") {
      throw new Error(
        `Reading sensitive files is blocked to prevent secret leaks. ` +
        `Use the dedicated secrets tool instead.`
      );
    }

    if (["Write", "Edit", "write", "edit"].includes(input.tool)) {
      throw new Error(
        `Editing sensitive files via standard tools is blocked. ` +
        `Use the dedicated secrets management tool.`
      );
    }
  }
}
```

---

## Secret Redaction

```typescript
const SECRET_PATTERNS = [
  /(?:api[_-]?key|apikey)[=:]\s*["']?([^"'\s]+)/gi,
  /(?:password|passwd|pwd)[=:]\s*["']?([^"'\s]+)/gi,
  /(?:token|bearer)[=:]\s*["']?([^"'\s]+)/gi,
  /(?:secret|private)[_-]?key[=:]\s*["']?([^"'\s]+)/gi,
];

function redactSecrets(text: string): string {
  let redacted = text;

  for (const pattern of SECRET_PATTERNS) {
    redacted = redacted.replace(pattern, (match, secret) => {
      const visible = secret.slice(0, 4);
      return match.replace(secret, `${visible}****[REDACTED]`);
    });
  }

  return redacted;
}
```

---

## Common Dangerous Patterns

| Category | Pattern | Risk |
|----------|---------|------|
| Git | `reset --hard` | Destroys uncommitted work |
| Git | `clean -f` | Removes untracked files |
| Git | `push --force` | Destroys remote history |
| Git | `checkout -- .` | Discards all changes |
| Filesystem | `rm -rf /` | System destruction |
| Filesystem | `rm -rf ~` | Home directory deletion |
| Filesystem | `chmod 777` | Security vulnerability |

---

## Source Reference

- `safety-net/src/` - Command interception
- `envsitter-guard/src/` - Sensitive file protection
- `oh-my-opencode/src/hooks/` - Tool blocking
