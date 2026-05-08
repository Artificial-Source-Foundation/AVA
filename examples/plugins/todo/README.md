# Todo Sample Plugin

This is AVA's minimal local plugin sample. It demonstrates:

- a `plugin.json` manifest
- a POSIX shell entrypoint invoked as `/bin/sh plugin.sh`
- one plugin command: `status`
- one plugin tool: `todo_add`
- one static prompt and one static skill
- one non-mutating event hook for `tool.result`

Install it into a project workspace:

```sh
mkdir -p .ava/plugins
cp -R examples/plugins/todo .ava/plugins/com.example.todo
```

Inspect and run it from AVA:

```text
/plugins validate .ava/plugins/com.example.todo/plugin.json
/plugins inspect com.example.todo
/plugins prompt com.example.todo todo-review
/plugins skill com.example.todo todo-triage
/plugins enable com.example.todo
/plugin run com.example.todo status {}
/plugins disable com.example.todo
```

The shell entrypoint intentionally keeps parsing simple so the protocol is easy to see. Use a real JSON parser for production plugins.
