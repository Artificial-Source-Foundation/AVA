# AVA Usage

## Starting AVA

```sh
ava
ava --mode plan
ava --continue
ava --session <id-or-prefix>
```

`--continue` resumes the newest session for the current workspace. `--session` resumes an exact ID or a unique prefix. On exit, AVA prints the command needed to resume the current session.

## Modes

- Build mode: normal coding mode. Workspace edits are allowed unless a path or command is risky.
- Plan mode: read/search is allowed, but source-code mutation is denied. Planning markdown may be written.

Press Tab in the TUI or use `/mode` to switch.

## Commands

- `/help`: show commands
- `/mode`: toggle build/plan mode
- `/sessions`: list sessions for this workspace
- `/read <path>`: read a file
- `/write <path> <text>`: write a file
- `/glob <pattern>`: list readable matching files
- `/grep <text> [glob]`: search readable files for literal text
- `/bash <command>`: run a conservative permissioned command
- `/quit`: exit

## Non-TTY Mode

When stdin/stdout are not both terminals, AVA uses a line shell instead of the TUI. This keeps tests and scripts non-interactive.

```sh
printf '/sessions\n/quit\n' | ava --continue
```

## Current 0.1 Limits

- OpenAI is the only provider.
- The HTTP transport uses the local `curl` executable.
- Tool results are returned after the provider turn completes; there is no async streaming UI yet.
- Permission `ask` decisions are currently denied by backend tools until a future prompt UI exists.
- `question` does not open a modal in 0.1; the assistant should ask the user directly.
