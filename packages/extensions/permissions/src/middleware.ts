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
import { createLogger } from '@ava/core-v2/logger'
import { buildApprovalKey, createDynamicRuleStore } from './dynamic-rules.js'
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
const dynamicRuleStore = createDynamicRuleStore()
const log = createLogger('permissions')

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
  dynamicRuleStore.clear()
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

// ─── Middleware ──────────────────────────────────────────────────────────────

export function createPermissionMiddleware(bus?: MessageBus): ToolMiddleware {
  return {
    name: 'ava-permissions',
    priority: 4,

    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      const { toolName, args } = ctx
      const path = (args.path ?? args.filePath ?? '') as string
      const command = typeof args.command === 'string' ? args.command : undefined

      const allow = (reason: string): undefined => {
        log.info('Decision', {
          tool: toolName,
          action: 'allow',
          reason,
          ...(command ? { command } : {}),
        })
        return undefined
      }

      const block = (reason: string, message: string): ToolMiddlewareResult => {
        log.warn('Blocked', {
          tool: toolName,
          reason,
          ...(command ? { command } : {}),
        })
        return { blocked: true, reason: message }
      }

      dynamicRuleStore.startSession(ctx.ctx.sessionId)

      // 1. Always block: .git writes
      if (
        isGitPath(path) &&
        toolName !== 'read_file' &&
        toolName !== 'glob' &&
        toolName !== 'grep'
      ) {
        return block('git_path_protected', 'Cannot modify .git directory')
      }

      // 2. Always block: node_modules writes
      if (isNodeModulesWrite(toolName, args)) {
        return block('node_modules_protected', 'Cannot write to node_modules')
      }

      // 3. Always block: destructive rm -rf
      if (toolName === 'bash' && isDangerousCommand(args)) {
        return block('dangerous_command', 'Destructive rm -rf commands are blocked')
      }

      // 4. Blocked patterns (upgraded to glob matching)
      if (isBlockedByPattern(args)) {
        return block('blocked_pattern', 'Path matches a blocked pattern')
      }

      // 5. Permission mode check (when set, takes priority over individual booleans)
      if (settings.permissionMode) {
        const mode = settings.permissionMode as PermissionMode
        if (mode === 'suggest') {
          // Suggest mode blocks all tool execution
          return block('suggest_mode', 'Suggest mode — tool execution disabled')
        }
        if (isToolAutoApproved(toolName, mode)) return allow(`permission_mode_${mode}`)
        // Not auto-approved by mode — fall through to bus/fallback approval
      } else {
        // 6. YOLO mode (legacy boolean): approve everything not blocked above
        if (settings.yolo) return allow('yolo_mode')
      }

      // 7. Always-approved list check (with arity fingerprinting for bash)
      if (isAlwaysApproved(toolName, args)) {
        return allow('always_approved')
      }

      // 7b. Session-scoped dynamic rules learned from prior explicit approvals.
      if (dynamicRuleStore.allows(ctx.ctx.sessionId, toolName, args)) {
        return allow('session_dynamic_rule')
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
          if (rule.decision === 'allow') return allow(`policy_allow:${rule.name}`)
          if (rule.decision === 'deny') {
            return block(
              `policy_deny:${rule.name}`,
              rule.reason ?? `Denied by policy rule ${rule.name}`
            )
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
        if (native?.action === 'allow') return allow('rust_permission_allow')
        if (native?.action === 'deny') {
          return block('rust_permission_deny', 'Denied by Rust permission policy')
        }

        const rule = evaluateToolRules(toolName, path || undefined, settings.toolRules)
        if (rule) {
          if (rule.action === 'allow') return allow(`tool_rule_allow:${rule.tool}`)
          if (rule.action === 'deny') {
            return block(
              `tool_rule_deny:${rule.tool}`,
              rule.reason ?? `Denied by tool rule for ${rule.tool}`
            )
          }
          // 'ask' falls through to bus approval below
        }
      }

      const risk = classifyRisk(toolName, args)

      // Skip legacy boolean checks when permission mode is set
      if (!settings.permissionMode) {
        // 10. Auto-approve reads
        if (risk === 'low' && settings.autoApproveReads) return allow('auto_approve_reads')

        // 11. Smart-approve: safe bash commands + trusted paths
        if (settings.smartApprove) {
          if (toolName === 'bash') {
            const command = (args.command ?? '') as string
            if (isSafeBashCommand(command)) return allow('smart_approve_safe_bash')
          }
          if (
            path &&
            settings.trustedPaths.length > 0 &&
            isInTrustedPath(path, settings.trustedPaths)
          ) {
            return allow('smart_approve_trusted_path')
          }
        }

        // 12. Auto-approve writes if configured
        if (risk === 'medium' && settings.autoApproveWrites) return allow('auto_approve_writes')

        // 13. Auto-approve commands if configured
        if (toolName === 'bash' && settings.autoApproveCommands) {
          if (!isSudoCommand(args)) return allow('auto_approve_commands')
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
          return block('user_denied', response.reason ?? 'Denied by user')
        }
        // Handle "always approve" — store arity-based fingerprint for bash commands
        if (response.alwaysApprove) {
          dynamicRuleStore.learn(ctx.ctx.sessionId, toolName, args)
        }
        log.info('User approved', {
          tool: toolName,
          action: 'allow',
          reason: 'user_approved',
          always: Boolean(response.alwaysApprove),
          ...(command ? { command } : {}),
        })
        return undefined
      }

      // 15. No approval handler — apply fallback blocking rules

      // Warn on .env files
      if (isEnvFile(args)) {
        return block('env_file_requires_confirmation', 'Accessing .env files requires confirmation')
      }

      // Warn on sudo
      if (toolName === 'bash' && isSudoCommand(args)) {
        return block('sudo_requires_confirmation', 'sudo requires confirmation')
      }

      return undefined
    },
  }
}
