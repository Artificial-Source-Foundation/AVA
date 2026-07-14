# AVA RPC v1 Python Client

`ava_rpc_client.py` is a small Python 3 standard-library client for the normative [`docs/rpc-protocol.md`](../../docs/rpc-protocol.md) contract. It launches AVA as a subprocess, splits stdout only on LF, decodes strict UTF-8, correlates out-of-order responses by `id`, records events by `request_id`, drains stderr separately, and releases pending calls when the process/pipe closes.

It has no Pi RPC, JSON-RPC 2.0, ACP, provider credential, or AVA session-file dependency.

```python
from ava_rpc_client import AvaRpcClient


def permissions(event):
    payload = event["payload"]
    print("permission:", payload["operation"], payload["target_path"])
    return ("deny", "example client is read-only")


def questions(event):
    return {"selected": event["payload"]["options"][0]["value"]}


with AvaRpcClient(
    ["ava", "--rpc"],
    cwd="/path/to/workspace",
    on_event=lambda event: print(event["name"]),
    on_permission=permissions,
    on_question=questions,
) as client:
    print(client.request("get_protocol"))
    print(client.request("get_state"))
    print(client.request("prompt", message="Summarize this workspace"))
```

Hooks may return `None` to leave a request pending for UI-driven use of `reply_permission()` or `reply_question()`. Both reply methods correlate their own RPC response, return its result object, and raise `RpcError` when AVA rejects the reply. Resolver hooks run off the stdout reader so waiting for these responses cannot block response dispatch. A resolver reply that loses a cancel/EOF race is ignored as benign. Other hook/reply failures are delivered to `client.hook_errors` and optional `on_hook_error`; they do not fail unrelated requests.

`request()` and resolver replies have no timeout by default: callers should use explicit operator policy, `cancel`, EOF, or process supervision rather than inventing a short permission/question timeout. Tests may pass a timeout to detect harness failure. If a call times out, its ID remains reserved until the late response is consumed or the connection closes; do not retry that ID early.

The CTest `ava_cli.rpc_python_client_e2e` runs this client against the real `ava --rpc` executable and the repository fake provider; it requires no live network or credentials.
