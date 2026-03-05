/**
 * Permission middleware — safety checks, auto-approve, arity fingerprinting.
 */

import { dispatchCompute } from '@ava/core-v2'
import type { MessageBus } from '@ava/core-v2/bus'
import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'
import { extractCommandPrefix } from './arity.js'
import { parseBashTokens } from './bash-parser.js'
import { isToolAutoApproved, type PermissionMode } from './modes.js'
import {
  classifyRisk,
  DEFAULT_SETTINGS,
  type DeclarativePolicyRule,
  type PermissionRequest,
  type PermissionResponse,
  type PermissionSettings,
  SAFE_BASH_PATTERNS,
  type ToolPermissionRule,
} from './types.js'

let settings: PermissionSettings = { ...DEFAULT_SETTINGS }
let activeSessionId: string | null = null

interface NativePermissionPattern {
  type: 'any' | 'glob' | 'regex' | 'path'
  value?: string
}

interface NativePermissionRule {
  tool: NativePermissionPattern
  args: NativePermissionPattern
  action: 'allow' | 'ask' | 'deny'
}

interface NativePermissionResult {
  action: 'allow' | 'ask' | 'deny'
}

function isNativePermissionsEnabled(): boolean {
  return process.env.AVA_RUST_PERMISSIONS !== '0'
}

function serializePermissionArgs(args: Record<string, unknown>): string[] {
  const out: string[] = []
  const command = typeof args.command === 'string' ? args.command : ''
  const path = typeof args.path === 'string' ? args.path : ''
  const filePath = typeof args.filePath === 'string' ? args.filePath : ''

  if (command) out.push(command)
  if (path) out.push(path)
  if (filePath) out.push(filePath)

  if (out.length === 0) {
    out.push(JSON.stringify(args, Object.keys(args).sort()))
  }

  return out
}

function toNativeRules(rules: ToolPermissionRule[]): NativePermissionRule[] {
  return rules
    .filter((rule) => !rule.paths || rule.paths.length === 0)
    .map((rule) => ({
      tool: rule.tool === '*' ? { type: 'any' } : { type: 'glob', value: rule.tool },
      args: { type: 'any' },
      action: rule.action,
    }))
}

async function evaluateNativePermission(
  workspaceRoot: string,
  toolName: string,
  args: Record<string, unknown>,
  rules: ToolPermissionRule[]
): Promise<NativePermissionResult | null> {
  if (!isNativePermissionsEnabled()) {
    return null
  }

  const nativeRules = toNativeRules(rules)
  if (nativeRules.length === 0) {
    return null
  }

  try {
    return await dispatchCompute<NativePermissionResult | null>(
      'evaluate_permission',
      {
        workspaceRoot,
        rules: nativeRules,
        tool: toolName,
        args: serializePermissionArgs(args),
      },
      async () => null
    )
  } catch {
    return null
  }
}

export function updateSettings(partial: Partial<PermissionSettings>): void {
  // Filter out undefined values to avoid overwriting defaults (e.g. blockedPatterns: [])
  const defined = Object.fromEntries(Object.entries(partial).filter(([, v]) => v !== undefined))
  settings = { ...settings, ...defined }
}

export function getSettings(): PermissionSettings {
  return { ...settings }
}

export function resetSettings(): void {
  settings = { ...DEFAULT_SETTINGS }
}

// ─── Glob Matching ─────────────────────────────────────────────────────────

/** Simple glob matching: supports *, **, and ? */
export function matchesGlob(value: string, pattern: string): boolean {
  const regex = pattern
    .replace(/[.+^${}()|[\]\\]/g, '\\$&') // escape regex special chars
    .replace(/\*\*/g, '§§') // placeholder for **
    .replace(/\*/g, '[^/]*') // * matches anything except /
    .replace(/§§/g, '.*') // ** matches anything
    .replace(/\?/g, '.') // ? matches single char
  return new RegExp(`^${regex}$`).test(value)
}

export function matchesAnyGlob(value: string, patterns: string[]): boolean {
  return patterns.some((p) => matchesGlob(value, p))
}

// ─── Per-Tool Rule Evaluation ──────────────────────────────────────────────

/** Evaluate tool rules — first matching rule wins. */
export function evaluateToolRules(
  toolName: string,
  path: string | undefined,
  rules: ToolPermissionRule[]
): ToolPermissionRule | undefined {
  for (const rule of rules) {
    if (!matchesGlob(toolName, rule.tool)) continue
    // If rule has path restrictions, check them
    if (rule.paths && rule.paths.length > 0) {
      if (!path || !matchesAnyGlob(path, rule.paths)) continue
    }
    return rule
  }
  return undefined
}

function evaluateDeclarativeRules(
  toolName: string,
  args: Record<string, unknown>,
  path: string | undefined,
  permissionMode: string | undefined,
  rules: DeclarativePolicyRule[]
): DeclarativePolicyRule | undefined {
  for (const rule of rules) {
    if (!matchesGlob(toolName, rule.tool)) continue

    if (rule.modes && rule.modes.length > 0) {
      if (!permissionMode || !rule.modes.includes(permissionMode)) continue
    }

    if (rule.paths && rule.paths.length > 0) {
      if (!path || !matchesAnyGlob(path, rule.paths)) continue
    }

    if (rule.argsPattern) {
      const target =
        typeof args.command === 'string'
          ? args.command
          : JSON.stringify(args, Object.keys(args).sort())
      const re = new RegExp(rule.argsPattern)
      if (!re.test(target)) continue
    }

    return rule
  }

  return undefined
}

// ─── Smart-Approve Helpers ─────────────────────────────────────────────────

export function isSafeBashCommand(command: string): boolean {
  const trimmed = command.trim()
  return SAFE_BASH_PATTERNS.some((pattern) => pattern.test(trimmed))
}

export function isInTrustedPath(filePath: string, trusted: string[]): boolean {
  return matchesAnyGlob(filePath, trusted)
}

// ─── Path Checks ────────────────────────────────────────────────────────────

function isGitPath(path: string): boolean {
  return (
    path.includes('/.git/') ||
    path.includes('/.git') ||
    path.endsWith('/.git') ||
    path.startsWith('.git/') ||
    path === '.git'
  )
}

function isNodeModulesWrite(toolName: string, args: Record<string, unknown>): boolean {
  if (toolName === 'read_file' || toolName === 'glob' || toolName === 'grep') return false
  const path = (args.path ?? args.filePath ?? '') as string
  return path.includes('/node_modules/') || path.startsWith('node_modules/')
}

function isEnvFile(args: Record<string, unknown>): boolean {
  const path = (args.path ?? args.filePath ?? '') as string
  const name = path.split('/').pop() ?? ''
  return name.startsWith('.env')
}

function isDangerousCommand(args: Record<string, unknown>): boolean {
  const cmd = (args.command ?? '') as string
  return (
    /rm\s+-rf\s+[/~]/.test(cmd) ||
    /rm\s+-rf\s+\*/.test(cmd) ||
    /(?:^|\s)(mkfs|dd|shutdown|reboot)(?:\s|$)/.test(cmd) ||
    /curl\s+[^|]*\|\s*(bash|sh)/.test(cmd)
  )
}

function isSudoCommand(args: Record<string, unknown>): boolean {
  const cmd = (args.command ?? '') as string
  return cmd.trimStart().startsWith('sudo ')
}

function isBlockedByPattern(args: Record<string, unknown>): boolean {
  if (!settings.blockedPatterns?.length) return false
  const path = (args.path ?? args.filePath ?? '') as string
  return settings.blockedPatterns.some(
    (p) => matchesGlob(path, `**/${p}/**`) || matchesGlob(path, `**/${p}`) || path.includes(p)
  )
}

/**
 * Build a fingerprint key for a tool call.
 * For bash commands, uses the command prefix (e.g., "bash:git:status").
 * For other tools, returns the tool name as-is.
 */
export function buildApprovalKey(toolName: string, args: Record<string, unknown>): string {
  if (toolName === 'bash') {
    const command = (args.command ?? '') as string
    if (command) {
      const tokens = parseBashTokens(command)
      const prefix = extractCommandPrefix(tokens)
      if (prefix.length > 0) {
        return `${toolName}:${prefix.join(':')}`
      }
    }
  }
  return toolName
}

/**
 * Check if a tool call matches any entry in the always-approved list.
 * Compares the fingerprint key against stored keys.
 */
function isAlwaysApproved(toolName: string, args: Record<string, unknown>): boolean {
  if (!settings.alwaysApproved?.length) return false

  const key = buildApprovalKey(toolName, args)

  // Direct match (exact key or plain tool name)
  if (settings.alwaysApproved.includes(key)) return true

  // Backward compat: plain tool name also matches (e.g., "write_file" still works)
  if (key !== toolName && settings.alwaysApproved.includes(toolName)) return true

  // Wildcard support for generalized approvals (e.g., bash:git:*)
  if (toolName === 'bash') {
    const wildcardKey = key.split(':').slice(0, 2).join(':') + ':*'
    if (settings.alwaysApproved.includes(wildcardKey)) return true
  }

  return false
}

function generalizedApprovalKeys(toolName: string, args: Record<string, unknown>): string[] {
  const exact = buildApprovalKey(toolName, args)
  if (toolName !== 'bash') return [exact]
  if (isDangerousCommand(args) || isSudoCommand(args)) return [exact]

  const command = ((args.command ?? '') as string).trim()
  const tokens = parseBashTokens(command)
  const prefix = extractCommandPrefix(tokens)
  if (prefix.length < 2) return [exact]

  if (prefix[0] === 'git') {
    const safeSubcommands = new Set(['status', 'log', 'diff', 'show'])
    if (safeSubcommands.has(prefix[1] ?? '')) {
      return ['bash:git:*', exact]
    }
  }

  return [exact]
}

// ─── Middleware ──────────────────────────────────────────────────────────────

export function createPermissionMiddleware(bus?: MessageBus): ToolMiddleware {
  return {
    name: 'ava-permissions',
    priority: 0, // Runs first — before all other middleware

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      const { toolName, args } = ctx
      const path = (args.path ?? args.filePath ?? '') as string

      // Reset learned approvals per session.
      if (activeSessionId !== ctx.ctx.sessionId) {
        activeSessionId = ctx.ctx.sessionId
        settings = {
          ...settings,
          alwaysApproved: [],
        }
      }

      // 1. Always block: .git writes
      if (
        isGitPath(path) &&
        toolName !== 'read_file' &&
        toolName !== 'glob' &&
        toolName !== 'grep'
      ) {
        return { blocked: true, reason: 'Cannot modify .git directory' }
      }

      // 2. Always block: node_modules writes
      if (isNodeModulesWrite(toolName, args)) {
        return { blocked: true, reason: 'Cannot write to node_modules' }
      }

      // 3. Always block: destructive rm -rf
      if (toolName === 'bash' && isDangerousCommand(args)) {
        return { blocked: true, reason: 'Destructive rm -rf commands are blocked' }
      }

      // 4. Blocked patterns (upgraded to glob matching)
      if (isBlockedByPattern(args)) {
        return { blocked: true, reason: 'Path matches a blocked pattern' }
      }

      // 5. Permission mode check (when set, takes priority over individual booleans)
      if (settings.permissionMode) {
        const mode = settings.permissionMode as PermissionMode
        if (mode === 'suggest') {
          // Suggest mode blocks all tool execution
          return { blocked: true, reason: 'Suggest mode — tool execution disabled' }
        }
        if (isToolAutoApproved(toolName, mode)) return undefined
        // Not auto-approved by mode — fall through to bus/fallback approval
      } else {
        // 6. YOLO mode (legacy boolean): approve everything not blocked above
        if (settings.yolo) return undefined
      }

      // 7. Always-approved list check (with arity fingerprinting for bash)
      if (isAlwaysApproved(toolName, args)) {
        return undefined
      }

      // 8. Per-tool rules (first match wins)
      if (settings.declarativePolicyRules?.length) {
        const rule = evaluateDeclarativeRules(
          toolName,
          args,
          path || undefined,
          settings.permissionMode,
          settings.declarativePolicyRules
        )
        if (rule) {
          if (rule.decision === 'allow') return undefined
          if (rule.decision === 'deny') {
            return {
              blocked: true,
              reason: rule.reason ?? `Denied by policy rule ${rule.name}`,
            }
          }
          // 'ask' falls through to approval flow
        }
      }

      // 9. Legacy per-tool rules (first match wins)
      if (settings.toolRules?.length) {
        const native = await evaluateNativePermission(
          ctx.ctx.workingDirectory,
          toolName,
          args,
          settings.toolRules
        )
        if (native?.action === 'allow') return undefined
        if (native?.action === 'deny') {
          return { blocked: true, reason: 'Denied by Rust permission policy' }
        }

        const rule = evaluateToolRules(toolName, path || undefined, settings.toolRules)
        if (rule) {
          if (rule.action === 'allow') return undefined
          if (rule.action === 'deny') {
            return { blocked: true, reason: rule.reason ?? `Denied by tool rule for ${rule.tool}` }
          }
          // 'ask' falls through to bus approval below
        }
      }

      const risk = classifyRisk(toolName, args)

      // Skip legacy boolean checks when permission mode is set
      if (!settings.permissionMode) {
        // 10. Auto-approve reads
        if (risk === 'low' && settings.autoApproveReads) return undefined

        // 11. Smart-approve: safe bash commands + trusted paths
        if (settings.smartApprove) {
          if (toolName === 'bash') {
            const command = (args.command ?? '') as string
            if (isSafeBashCommand(command)) return undefined
          }
          if (
            path &&
            settings.trustedPaths.length > 0 &&
            isInTrustedPath(path, settings.trustedPaths)
          ) {
            return undefined
          }
        }

        // 12. Auto-approve writes if configured
        if (risk === 'medium' && settings.autoApproveWrites) return undefined

        // 13. Auto-approve commands if configured
        if (toolName === 'bash' && settings.autoApproveCommands) {
          if (!isSudoCommand(args)) return undefined
        }
      }

      // 14. Bus-based approval with alwaysApprove support
      if (bus?.hasSubscribers('permission:request')) {
        const response = await bus.request<PermissionRequest, PermissionResponse>(
          { type: 'permission:request', toolName, args, risk },
          'permission:response',
          120_000
        )
        if (!response.approved) {
          return { blocked: true, reason: response.reason ?? 'Denied by user' }
        }
        // Handle "always approve" — store arity-based fingerprint for bash commands
        if (response.alwaysApprove) {
          const keys = generalizedApprovalKeys(toolName, args)
          const unique = new Set(settings.alwaysApproved)
          for (const key of keys) unique.add(key)
          settings = {
            ...settings,
            alwaysApproved: [...unique],
          }
        }
        return undefined
      }

      // 15. No approval handler — apply fallback blocking rules

      // Warn on .env files
      if (isEnvFile(args)) {
        return { blocked: true, reason: 'Accessing .env files requires confirmation' }
      }

      // Warn on sudo
      if (toolName === 'bash' && isSudoCommand(args)) {
        return { blocked: true, reason: 'sudo requires confirmation' }
      }

      return undefined
    },
  }
}
