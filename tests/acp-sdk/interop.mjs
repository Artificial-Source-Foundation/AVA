#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { Readable, Writable } from "node:stream";

const MAX_STDERR_BYTES = 16 * 1024;
const OPERATION_TIMEOUT_MS = 5_000;
const PROCESS_TIMEOUT_MS = 7_000;
const FAKE_KEY = "AVA_ACP_INTEROP_FAKE_KEY_NOT_A_SECRET";
const activeOwnedProcesses = new Set();
let signalCleanupStarted = false;

function scrubParentEnvironment() {
  for (const name of Object.keys(process.env)) delete process.env[name];
}

function parseArgs(argv) {
  const result = { required: false };
  for (let index = 0; index < argv.length; ++index) {
    const argument = argv[index];
    if (argument === "--required") {
      result.required = true;
      continue;
    }
    if (!["--ava", "--fake-provider", "--root"].includes(argument) || index + 1 >= argv.length) {
      throw new Error(`unknown or incomplete argument: ${argument}`);
    }
    result[argument.slice(2).replace("fake-provider", "fakeProvider")] = argv[++index];
  }
  if (!result.ava || !result.fakeProvider) {
    throw new Error("usage: interop.mjs --ava PATH --fake-provider PATH [--root PATH] [--required]");
  }
  return result;
}

function missingSdk(error) {
  return error?.code === "ERR_MODULE_NOT_FOUND";
}

async function loadSdk(required) {
  try {
    return await import("@agentclientprotocol/sdk");
  } catch (error) {
    if (!required && missingSdk(error)) {
      console.error("SKIP: install tests/acp-sdk dependencies with npm ci --ignore-scripts --no-audit --no-fund");
      process.exitCode = 77;
      return null;
    }
    throw error;
  }
}

function withDeadline(promise, label, timeoutMs = OPERATION_TIMEOUT_MS, onTimeout = undefined) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => {
      try {
        onTimeout?.();
      } finally {
        reject(new Error(`${label} timed out after ${timeoutMs}ms`));
      }
    }, timeoutMs);
    timer.unref?.();
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

function boundedCapture(stream) {
  let content = Buffer.alloc(0);
  let truncated = false;
  stream.on("data", (chunk) => {
    if (content.length >= MAX_STDERR_BYTES) {
      truncated = true;
      return;
    }
    const bytes = Buffer.from(chunk);
    const retained = bytes.subarray(0, MAX_STDERR_BYTES - content.length);
    content = Buffer.concat([content, retained]);
    truncated ||= retained.length !== bytes.length;
  });
  return () => `${content.toString("utf8")}${truncated ? "\n<stderr truncated>" : ""}`;
}

function spawnOwned(command, args, options) {
  const child = spawn(command, args, {
    ...options,
    detached: process.platform !== "win32",
    stdio: options.stdio ?? ["ignore", "ignore", "pipe"],
  });
  const stderr = boundedCapture(child.stderr);
  const exited = new Promise((resolve) => {
    child.once("error", (error) => resolve({ code: null, signal: null, error }));
    child.once("exit", (code, signal) => resolve({ code, signal, error: null }));
  });
  const owned = { child, exited, stderr };
  activeOwnedProcesses.add(owned);
  return owned;
}

function processAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return error.code !== "ESRCH";
  }
}

function groupAlive(pid) {
  if (process.platform === "win32") return processAlive(pid);
  try {
    process.kill(-pid, 0);
    return true;
  } catch (error) {
    return error.code !== "ESRCH";
  }
}

function signalOwned(owned, signal) {
  if (!owned?.child?.pid) return;
  try {
    if (process.platform === "win32") owned.child.kill(signal);
    else process.kill(-owned.child.pid, signal);
  } catch (error) {
    if (error.code !== "ESRCH") throw error;
  }
}

async function waitForGroupExit(pid, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline && groupAlive(pid)) {
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  return !groupAlive(pid);
}

async function terminateOwned(owned) {
  if (!owned?.child?.pid) return;
  if (groupAlive(owned.child.pid)) {
    signalOwned(owned, "SIGTERM");
    if (!await waitForGroupExit(owned.child.pid, 750)) {
      signalOwned(owned, "SIGKILL");
      assert.equal(await waitForGroupExit(owned.child.pid, 1_500), true, "owned process group survived SIGKILL");
    }
  }
  await withDeadline(owned.exited, "process reap", 1_500);
}

async function assertGroupExited(pid) {
  if (!pid) return;
  const deadline = Date.now() + 1_000;
  while (Date.now() < deadline && groupAlive(pid)) {
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  assert.equal(groupAlive(pid), false, `owned process group ${pid} survived cleanup`);
}

function installSignalCleanup() {
  for (const [signal, exitCode] of [["SIGINT", 130], ["SIGTERM", 143]]) {
    process.once(signal, () => {
      if (signalCleanupStarted) return;
      signalCleanupStarted = true;
      const forced = setTimeout(() => {
        for (const owned of activeOwnedProcesses) signalOwned(owned, "SIGKILL");
        process.exit(exitCode);
      }, 3_000);
      Promise.allSettled([...activeOwnedProcesses].map((owned) => terminateOwned(owned))).finally(() => {
        clearTimeout(forced);
        process.exit(exitCode);
      });
    });
  }
}

function cleanEnvironment(roots, providerPort = undefined) {
  const env = {
    HOME: roots.home,
    XDG_CONFIG_HOME: roots.config,
    XDG_DATA_HOME: roots.data,
    XDG_STATE_HOME: roots.state,
    XDG_CACHE_HOME: roots.cache,
    TMPDIR: roots.tmp,
    PATH: "/usr/bin:/bin",
    LANG: "C.UTF-8",
    LC_ALL: "C.UTF-8",
    NO_COLOR: "1",
    AVA_SESSION_TITLES: "off",
  };
  if (providerPort !== undefined) {
    env.MOONSHOT_BASE_URL = `http://127.0.0.1:${providerPort}`;
    env.MOONSHOT_API_KEY = FAKE_KEY;
  }
  return env;
}

async function makeRoots(base, name) {
  const scenario = path.join(base, name);
  const roots = {
    scenario,
    home: path.join(scenario, "home"),
    config: path.join(scenario, "xdg-config"),
    data: path.join(scenario, "xdg-data"),
    state: path.join(scenario, "xdg-state"),
    cache: path.join(scenario, "xdg-cache"),
    tmp: path.join(scenario, "tmp"),
    sessions: path.join(scenario, "xdg-state", "ava", "sessions"),
    workspace: path.join(scenario, "workspace"),
  };
  await Promise.all(Object.values(roots).map((entry) => mkdir(entry, { recursive: true })));
  return roots;
}

async function configureModel(roots, supportsTools = false) {
  const directory = path.join(roots.config, "ava");
  await mkdir(directory, { recursive: true });
  await writeFile(path.join(directory, "models.json"), `${JSON.stringify({
    default_provider: "moonshot",
    default_model: "acp-interop",
    models: [{
      provider: "moonshot",
      id: "acp-interop",
      name: "ACP Interop",
      family: "fake",
      context_window_tokens: 8192,
      max_output_tokens: 1024,
      supports_tools: supportsTools,
      supports_streaming: false,
      input_modalities: ["text"],
      output_modalities: ["text"],
    }],
  })}\n`, { mode: 0o600 });
}

async function waitForPort(portFile, provider) {
  const deadline = Date.now() + 3_000;
  while (Date.now() < deadline) {
    if (existsSync(portFile)) {
      const value = Number((await readFile(portFile, "utf8")).trim());
      if (Number.isInteger(value) && value > 0 && value <= 65535) return value;
    }
    const exited = await Promise.race([
      provider.exited.then((result) => ({ result })),
      new Promise((resolve) => setTimeout(() => resolve(null), 20)),
    ]);
    if (exited) throw new Error(`fake provider exited during startup: ${JSON.stringify(exited.result)}`);
  }
  throw new Error("fake provider did not publish its loopback port");
}

async function waitForLog(logFile, needle) {
  const deadline = Date.now() + 3_000;
  while (Date.now() < deadline) {
    const content = await readFile(logFile, "utf8").catch(() => "");
    if (content.includes(needle)) return content;
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  throw new Error(`provider log did not contain ${needle}`);
}

async function sdkRequest(ctx, acp, method, params, label = method) {
  const cancellation = new AbortController();
  return withDeadline(
    ctx.request(method, params, { cancellationSignal: cancellation.signal }),
    label,
    OPERATION_TIMEOUT_MS,
    () => cancellation.abort(new Error(`${label} deadline`)),
  );
}

async function sdkNotify(ctx, method, params, label = method) {
  return withDeadline(ctx.notify(method, params), label);
}

function initializeParams(acp, capabilities = {}) {
  return {
    protocolVersion: acp.PROTOCOL_VERSION,
    clientCapabilities: capabilities,
    clientInfo: { name: "ava-official-sdk-interop", version: "1" },
  };
}

async function runScenario({ acp, args, base, name, providerScenario, providerTarget = "unused", delayMs = 0, supportsTools = false, app, workflow }) {
  const roots = await makeRoots(base, name);
  await configureModel(roots, supportsTools);
  const providerRoot = path.join(roots.scenario, "provider");
  await mkdir(providerRoot, { recursive: true });
  const portFile = path.join(providerRoot, "port");
  const requestLog = path.join(providerRoot, "requests.log");
  const provider = spawnOwned(
    path.resolve(args.fakeProvider),
    [portFile, requestLog, String(delayMs), providerScenario, providerTarget],
    { env: cleanEnvironment(roots), cwd: roots.workspace },
  );
  let ava;
  try {
    const port = await waitForPort(portFile, provider);
    ava = spawnOwned(path.resolve(args.ava), ["--acp"], {
      env: cleanEnvironment(roots, port),
      cwd: roots.workspace,
      stdio: ["pipe", "pipe", "pipe"],
    });
    const stream = acp.ndJsonStream(Writable.toWeb(ava.child.stdin), Readable.toWeb(ava.child.stdout));
    const result = await withDeadline(
      app.connectWith(stream, (ctx) => workflow(ctx, roots, requestLog)),
      `${name} SDK connection`,
      18_000,
    );

    // connectWith closes SDK ownership; AVA still owns protocol EOF and must see it explicitly.
    if (!ava.child.stdin.destroyed && !ava.child.stdin.writableEnded) ava.child.stdin.end();
    const avaExit = await withDeadline(ava.exited, `${name} AVA exit`, PROCESS_TIMEOUT_MS);
    assert.equal(avaExit.error, null, `${name} AVA spawn failed: ${avaExit.error}`);
    assert.equal(avaExit.code, 0, `${name} AVA exited ${JSON.stringify(avaExit)}: ${ava.stderr()}`);
    assert.equal(ava.stderr(), "", `${name} AVA stderr was not clean`);

    const providerExit = await withDeadline(provider.exited, `${name} provider exit`, PROCESS_TIMEOUT_MS);
    assert.equal(providerExit.error, null, `${name} provider spawn failed: ${providerExit.error}`);
    assert.equal(providerExit.code, 0, `${name} provider exited ${JSON.stringify(providerExit)}: ${provider.stderr()}`);
    assert.equal(provider.stderr(), "", `${name} provider stderr was not clean`);
    await assertGroupExited(ava.child.pid);
    await assertGroupExited(provider.child.pid);
    return { result, roots, requestLog };
  } finally {
    if (ava) {
      await terminateOwned(ava);
      await assertGroupExited(ava.child.pid);
      activeOwnedProcesses.delete(ava);
    }
    await terminateOwned(provider);
    await assertGroupExited(provider.child.pid);
    activeOwnedProcesses.delete(provider);
  }
}

async function lifecycleScenario(acp, args, base) {
  const updates = [];
  const app = acp.client({ name: "ava-sdk-lifecycle" })
    .onNotification(acp.methods.client.session.update, ({ params }) => updates.push(params));
  await runScenario({
    acp,
    args,
    base,
    name: "lifecycle",
    providerScenario: "text",
    app,
    workflow: async (ctx, roots) => {
      const initialized = await sdkRequest(ctx, acp, acp.methods.agent.initialize, initializeParams(acp), "initialize");
      assert.equal(initialized.protocolVersion, 1);
      assert.equal(initialized.agentCapabilities.loadSession, false);
      assert.deepEqual(initialized.agentCapabilities.sessionCapabilities, { list: {}, resume: {}, close: {} });
      assert.deepEqual(initialized.agentCapabilities.promptCapabilities, { image: false, audio: false, embeddedContext: false });
      assert.deepEqual(initialized.authMethods, []);

      const created = await sdkRequest(ctx, acp, acp.methods.agent.session.new, { cwd: roots.workspace, mcpServers: [] }, "session/new");
      const prompted = await sdkRequest(ctx, acp, acp.methods.agent.session.prompt, {
        sessionId: created.sessionId,
        prompt: [{ type: "text", text: "official SDK lifecycle" }],
      }, "session/prompt");
      assert.equal(prompted.stopReason, "end_turn");
      assert.ok(updates.some((entry) => entry.sessionId === created.sessionId && entry.update.sessionUpdate === "agent_message_chunk"));

      const listed = await sdkRequest(ctx, acp, acp.methods.agent.session.list, {}, "session/list");
      assert.ok(listed.sessions.some((entry) => entry.sessionId === created.sessionId));
      assert.deepEqual(await sdkRequest(ctx, acp, acp.methods.agent.session.close, { sessionId: created.sessionId }, "session/close"), {});
      assert.deepEqual(await sdkRequest(ctx, acp, acp.methods.agent.session.resume, {
        sessionId: created.sessionId,
        cwd: roots.workspace,
        mcpServers: [],
      }, "session/resume"), {});
      assert.deepEqual(await sdkRequest(ctx, acp, acp.methods.agent.session.close, { sessionId: created.sessionId }, "session/close resumed"), {});
    },
  });
}

async function readToolScenario(acp, args, base) {
  const methods = [];
  const remoteContent = "OFFICIAL_SDK_REMOTE_FILE_CONTENT";
  let expectedSession;
  const target = path.join(base, "read-tool", "workspace", "client-read.txt");
  const app = acp.client({ name: "ava-sdk-read-tool" })
    .onRequest(acp.methods.client.session.requestPermission, ({ params }) => {
      methods.push(acp.methods.client.session.requestPermission);
      assert.equal(params.sessionId, expectedSession);
      assert.equal(params.toolCall.toolCallId, "call_read");
      assert.deepEqual(params.options.map((entry) => entry.optionId), ["allow_once", "allow_always", "reject_once", "reject_always"]);
      return { outcome: { outcome: "selected", optionId: "allow_once" } };
    })
    .onRequest(acp.methods.client.fs.readTextFile, ({ params }) => {
      methods.push(acp.methods.client.fs.readTextFile);
      assert.equal(params.sessionId, expectedSession);
      assert.equal(params.path, target);
      assert.equal(params.line, 1);
      assert.equal(params.limit, 201);
      return { content: remoteContent };
    });

  const scenario = await runScenario({
    acp,
    args,
    base,
    name: "read-tool",
    providerScenario: "read-tool",
    providerTarget: target,
    supportsTools: true,
    app,
    workflow: async (ctx, scenarioRoots) => {
      assert.equal(path.join(scenarioRoots.workspace, "client-read.txt"), target);
      await writeFile(target, "LOCAL_CONTENT_MUST_NOT_WIN\n");
      await sdkRequest(ctx, acp, acp.methods.agent.initialize, initializeParams(acp, {
        fs: { readTextFile: true, writeTextFile: false },
      }), "read initialize");
      const created = await sdkRequest(ctx, acp, acp.methods.agent.session.new, { cwd: scenarioRoots.workspace, mcpServers: [] }, "read session/new");
      expectedSession = created.sessionId;
      const result = await sdkRequest(ctx, acp, acp.methods.agent.session.prompt, {
        sessionId: created.sessionId,
        prompt: [{ type: "text", text: "read with the official client" }],
      }, "read session/prompt");
      assert.equal(result.stopReason, "end_turn");
      assert.deepEqual(methods, [acp.methods.client.session.requestPermission, acp.methods.client.fs.readTextFile]);
      await sdkRequest(ctx, acp, acp.methods.agent.session.close, { sessionId: created.sessionId }, "read session/close");
    },
  });
  const log = await readFile(scenario.requestLog, "utf8");
  assert.ok(log.includes(remoteContent), "provider did not receive official-client file content");
  assert.ok(!log.includes("LOCAL_CONTENT_MUST_NOT_WIN"), "provider received local file content instead of official-client content");
}

async function terminalScenario(acp, args, base) {
  const methods = [];
  const updates = [];
  let expectedSession;
  let expectedWorkspace;
  let releases = 0;
  const terminalId = "official-sdk-terminal";
  const app = acp.client({ name: "ava-sdk-terminal" })
    .onNotification(acp.methods.client.session.update, ({ params }) => updates.push(params.update))
    .onRequest(acp.methods.client.session.requestPermission, () => {
      methods.push(acp.methods.client.session.requestPermission);
      return { outcome: { outcome: "selected", optionId: "allow_once" } };
    })
    .onRequest(acp.methods.client.terminal.create, ({ params }) => {
      methods.push(acp.methods.client.terminal.create);
      assert.equal(params.sessionId, expectedSession);
      assert.equal(params.command, "touch");
      assert.deepEqual(params.args, ["terminal-e2e-marker"]);
      assert.deepEqual(params.env, []);
      assert.equal(params.cwd, expectedWorkspace);
      return { terminalId };
    })
    .onRequest(acp.methods.client.terminal.waitForExit, ({ params }) => {
      methods.push(acp.methods.client.terminal.waitForExit);
      assert.deepEqual(params, { sessionId: expectedSession, terminalId });
      return { exitCode: 0, signal: null };
    })
    .onRequest(acp.methods.client.terminal.output, ({ params }) => {
      methods.push(acp.methods.client.terminal.output);
      assert.deepEqual(params, { sessionId: expectedSession, terminalId });
      return { output: "OFFICIAL_SDK_TERMINAL_OUTPUT", truncated: false };
    })
    .onRequest(acp.methods.client.terminal.kill, () => {
      methods.push(acp.methods.client.terminal.kill);
      return {};
    })
    .onRequest(acp.methods.client.terminal.release, ({ params }) => {
      methods.push(acp.methods.client.terminal.release);
      assert.deepEqual(params, { sessionId: expectedSession, terminalId });
      ++releases;
      return {};
    });

  const scenario = await runScenario({
    acp,
    args,
    base,
    name: "terminal",
    providerScenario: "terminal-tool",
    supportsTools: true,
    app,
    workflow: async (ctx, roots) => {
      await sdkRequest(ctx, acp, acp.methods.agent.initialize, initializeParams(acp, { terminal: true }), "terminal initialize");
      const created = await sdkRequest(ctx, acp, acp.methods.agent.session.new, { cwd: roots.workspace, mcpServers: [] }, "terminal session/new");
      expectedSession = created.sessionId;
      expectedWorkspace = roots.workspace;
      const result = await sdkRequest(ctx, acp, acp.methods.agent.session.prompt, {
        sessionId: created.sessionId,
        prompt: [{ type: "text", text: "use the official client terminal" }],
      }, "terminal session/prompt");
      assert.equal(result.stopReason, "end_turn");
      await sdkRequest(ctx, acp, acp.methods.agent.session.close, { sessionId: created.sessionId }, "terminal session/close");
    },
  });
  const expectedMethods = [
    acp.methods.client.session.requestPermission,
    acp.methods.client.terminal.create,
    acp.methods.client.terminal.waitForExit,
    acp.methods.client.terminal.output,
    acp.methods.client.terminal.release,
  ];
  const providerLog = await readFile(scenario.requestLog, "utf8");
  const terminalDiagnostics = JSON.stringify({
    updates,
    providerRequests: (providerLog.match(/^--- request /gm) ?? []).length,
    providerAdvertisedTools: providerLog.includes('"tools"'),
  });
  assert.deepEqual(methods, expectedMethods, `terminal method sequence mismatch: ${terminalDiagnostics}`);
  assert.equal(releases, 1, "terminal must be released exactly once");
  assert.equal(existsSync(path.join(scenario.roots.workspace, "terminal-e2e-marker")), false, "AVA executed the terminal command locally");
  assert.ok((await readFile(scenario.requestLog, "utf8")).includes("OFFICIAL_SDK_TERMINAL_OUTPUT"));
}

async function cancellationScenario(acp, args, base) {
  const app = acp.client({ name: "ava-sdk-cancellation" });
  await runScenario({
    acp,
    args,
    base,
    name: "cancellation",
    providerScenario: "text",
    delayMs: 1_500,
    app,
    workflow: async (ctx, roots, requestLog) => {
      await sdkRequest(ctx, acp, acp.methods.agent.initialize, initializeParams(acp), "cancel initialize");
      const created = await sdkRequest(ctx, acp, acp.methods.agent.session.new, { cwd: roots.workspace, mcpServers: [] }, "cancel session/new");
      const prompt = sdkRequest(ctx, acp, acp.methods.agent.session.prompt, {
        sessionId: created.sessionId,
        prompt: [{ type: "text", text: "cancel this in-flight turn" }],
      }, "cancel session/prompt");
      await waitForLog(requestLog, "--- request 1 ---");
      await sdkNotify(ctx, acp.methods.agent.session.cancel, { sessionId: created.sessionId }, "session/cancel");
      assert.equal((await prompt).stopReason, "cancelled");
      const listed = await sdkRequest(ctx, acp, acp.methods.agent.session.list, {}, "post-cancel session/list");
      assert.ok(listed.sessions.some((entry) => entry.sessionId === created.sessionId), "connection was not usable after cancellation");
      await sdkRequest(ctx, acp, acp.methods.agent.session.close, { sessionId: created.sessionId }, "cancel session/close");
    },
  });
}

function redact(text, root) {
  return String(text).replaceAll(root, "<interop-root>").replaceAll(FAKE_KEY, "<fake-key>").slice(0, MAX_STDERR_BYTES);
}

async function main() {
  installSignalCleanup();
  const args = parseArgs(process.argv.slice(2));
  scrubParentEnvironment();
  const acp = await loadSdk(args.required);
  if (!acp) return 77;
  assert.equal(acp.PROTOCOL_VERSION, 1, "official SDK protocol pin drifted from v1");
  assert.equal(process.platform === "win32", false, "owned process-group interop harness currently requires POSIX");

  const parent = path.resolve(args.root ?? os.tmpdir());
  await mkdir(parent, { recursive: true });
  const base = await mkdtemp(path.join(parent, "ava-acp-sdk-"));
  try {
    await lifecycleScenario(acp, args, base);
    await readToolScenario(acp, args, base);
    await terminalScenario(acp, args, base);
    await cancellationScenario(acp, args, base);
    console.log("official @agentclientprotocol/sdk 1.2.1 interoperability checks passed");
    return 0;
  } catch (error) {
    console.error(redact(error?.stack ?? error, base));
    return 1;
  } finally {
    await rm(base, { recursive: true, force: true });
  }
}

try {
  process.exitCode = await main();
} catch (error) {
  console.error(String(error?.stack ?? error).replaceAll(process.cwd(), "<source-root>").replaceAll(FAKE_KEY, "<fake-key>").slice(0, MAX_STDERR_BYTES));
  process.exitCode = 1;
}
