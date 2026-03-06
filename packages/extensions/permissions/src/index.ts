/**
 * Permissions extension.
 * Registers a tool middleware for safety and permission checking.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { LLMProvider } from '@ava/core-v2/llm'
import { createPolicyCache, enforcePolicy, generatePolicy } from './conseca.js'
import {
  InspectionPipeline,
  PermissionInspector,
  RepetitionInspector,
  SecurityInspector,
} from './inspection-pipeline.js'
import { createPermissionMiddleware, updateSettings } from './middleware.js'
import { loadDeclarativePolicies } from './policy/index.js'
import { activate as activateRules } from './rules/index.js'
import { createSandboxMiddleware } from './sandbox-middleware.js'
import { createSmartApproveMiddleware } from './smart-approve.js'
import type { ToolPermissionRule } from './types.js'

function applySettings(raw: Record<string, unknown>): void {
  updateSettings({
    yolo: raw.yolo as boolean | undefined,
    autoApproveReads: raw.autoApproveReads as boolean | undefined,
    autoApproveWrites: raw.autoApproveWrites as boolean | undefined,
    autoApproveCommands: raw.autoApproveCommands as boolean | undefined,
    blockedPatterns: raw.blockedPatterns as string[] | undefined,
    trustedPaths: raw.trustedPaths as string[] | undefined,
    toolRules: raw.toolRules as ToolPermissionRule[] | undefined,
    smartApprove: raw.smartApprove as boolean | undefined,
    alwaysApproved: raw.alwaysApproved as string[] | undefined,
    permissionMode: raw.permissionMode as string | undefined,
  })
}

interface ConsecaSettings {
  enabled?: boolean
  provider?: LLMProvider
  model?: string
  availableTools?: string[]
}

const DEFAULT_CONSECA_PROVIDER: LLMProvider = 'openrouter'
const DEFAULT_CONSECA_MODEL = 'moonshotai/kimi-k2.5'
const DEFAULT_AVAILABLE_TOOLS = [
  'read',
  'glob',
  'grep',
  'write_file',
  'edit',
  'create_file',
  'delete_file',
  'bash',
]

function getConsecaSettings(raw: Record<string, unknown>): ConsecaSettings {
  const conseca = raw.conseca
  if (!conseca || typeof conseca !== 'object') {
    return {}
  }
  return conseca as ConsecaSettings
}

async function reloadPolicies(api: ExtensionAPI, cwd: string): Promise<void> {
  const loaded = await loadDeclarativePolicies(api.platform.fs, cwd)
  updateSettings({ declarativePolicyRules: loaded.rules })
  for (const warning of loaded.warnings) {
    api.log.warn(warning)
  }
}

export function activate(api: ExtensionAPI): Disposable {
  const rulesDisposable = activateRules(api)
  const policyCache = createPolicyCache()
  const securityInspector = new SecurityInspector()
  const repetitionInspector = new RepetitionInspector(3)
  let latestGoal = 'Complete the user request safely with least privilege.'
  let currentSettings: Record<string, unknown> = {}

  const getToolList = (value: unknown): string[] => {
    if (!Array.isArray(value)) return []
    return value.filter((item): item is string => typeof item === 'string' && item.length > 0)
  }

  const getInspectionPolicy = (): { allowlist?: string[]; denylist?: string[] } => {
    const inspection =
      currentSettings.inspection && typeof currentSettings.inspection === 'object'
        ? (currentSettings.inspection as Record<string, unknown>)
        : {}

    const allowlist = getToolList(inspection.allowlist ?? currentSettings.allowlist)
    const denylist = getToolList(inspection.denylist ?? currentSettings.denylist)

    return {
      allowlist: allowlist.length > 0 ? allowlist : undefined,
      denylist: denylist.length > 0 ? denylist : undefined,
    }
  }

  const inspectionMiddlewareDisposable = api.addToolMiddleware({
    name: 'ava-inspection-pipeline',
    priority: 1,
    async before(ctx) {
      const pipeline = new InspectionPipeline()
      pipeline.register(securityInspector)
      pipeline.register(new PermissionInspector(getInspectionPolicy()))
      pipeline.register(repetitionInspector)

      const result = await pipeline.inspect(ctx.toolName, ctx.args, ctx.ctx)
      if (result.action === 'allow') {
        return undefined
      }

      if (result.action === 'deny') {
        return {
          blocked: true,
          reason: result.reason,
        }
      }

      return {
        blocked: true,
        reason: `Approval required: ${result.reason}`,
      }
    },
  })

  const consecaMiddlewareDisposable = api.addToolMiddleware({
    name: 'ava-conseca',
    priority: 2,
    async before(ctx) {
      const consecaSettings = getConsecaSettings(currentSettings)
      if (consecaSettings.enabled !== true) {
        return undefined
      }

      const provider = consecaSettings.provider ?? DEFAULT_CONSECA_PROVIDER
      const model = consecaSettings.model ?? DEFAULT_CONSECA_MODEL
      const availableTools = consecaSettings.availableTools ?? DEFAULT_AVAILABLE_TOOLS
      const policy = await policyCache.getOrCreate(ctx.ctx.sessionId, async () =>
        generatePolicy(latestGoal, availableTools, ctx.ctx.workingDirectory, provider, model)
      )

      const verdict = enforcePolicy(policy, ctx.toolName, ctx.args)
      if (verdict.allowed) {
        return undefined
      }

      return {
        blocked: true,
        reason: verdict.reason ?? 'Blocked by generated security policy.',
      }
    },
  })

  // Register the permission middleware (pass bus for interactive approval)
  const smartApproveDisposable = api.addToolMiddleware(createSmartApproveMiddleware())
  const sandboxDisposable = api.addToolMiddleware(createSandboxMiddleware())
  const mwDisposable = api.addToolMiddleware(createPermissionMiddleware(api.bus))

  // Sync settings from the settings manager (may not exist yet)
  try {
    const settings = api.getSettings<Record<string, unknown>>('permissions')

    if (settings) {
      currentSettings = settings
      applySettings(settings)
    }
  } catch {
    // Settings category not registered yet — use defaults
  }

  // Listen for settings changes
  const settingsDisposable = api.onSettingsChanged('permissions', (s) => {
    currentSettings = s as Record<string, unknown>
    applySettings(currentSettings)
  })

  const agentStartDisposable = api.on('agent:start', (event) => {
    const payload = event as { goal?: string }
    if (typeof payload.goal === 'string' && payload.goal.length > 0) {
      latestGoal = payload.goal
    }
  })

  const sessionDisposable = api.on('session:opened', (event) => {
    const payload = event as { sessionId?: string; workingDirectory?: string }
    const cwd = payload.workingDirectory ?? process.cwd()
    if (payload.sessionId) {
      policyCache.clear(payload.sessionId)
    }
    void reloadPolicies(api, cwd)
  })

  return {
    dispose() {
      rulesDisposable.dispose()
      inspectionMiddlewareDisposable.dispose()
      consecaMiddlewareDisposable.dispose()
      smartApproveDisposable.dispose()
      sandboxDisposable.dispose()
      mwDisposable.dispose()
      settingsDisposable.dispose()
      agentStartDisposable.dispose()
      sessionDisposable.dispose()
    },
  }
}

export { ARITY_MAP, extractCommandPrefix } from './arity.js'
export type { BashTokens } from './bash-parser.js'
export { parseBashTokens } from './bash-parser.js'
export type { SecurityPolicy } from './conseca.js'
export { createPolicyCache, enforcePolicy, generatePolicy } from './conseca.js'
export {
  buildApprovalKey,
  createDynamicRuleStore,
  isDangerousToGeneralize,
} from './dynamic-rules.js'
export {
  InspectionPipeline,
  PermissionInspector,
  RepetitionInspector,
  SecurityInspector,
} from './inspection-pipeline.js'
export {
  createPermissionMiddleware,
  evaluateToolRules,
  getSettings,
  isInTrustedPath,
  isSafeBashCommand,
  matchesAnyGlob,
  matchesGlob,
  resetSettings,
  updateSettings,
} from './middleware.js'
export type { PermissionMode, PermissionModeConfig } from './modes.js'
export {
  getAllPermissionModes,
  getPermissionMode,
  isToolAutoApproved,
  PERMISSION_MODES,
} from './modes.js'
export { createSmartApproveMiddleware, READ_ONLY_TOOLS } from './smart-approve.js'
export type {
  PermissionRequest,
  PermissionResponse,
  PermissionSettings,
  PolicyRule,
  RiskLevel,
  ToolPermissionRule,
} from './types.js'
export { BUILTIN_RULES, classifyRisk, DEFAULT_SETTINGS, SAFE_BASH_PATTERNS } from './types.js'
