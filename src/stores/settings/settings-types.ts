/**
 * Settings Types
 * All type definitions for the settings store.
 * Pure types — no runtime code, no imports with side effects.
 */

import type { AgentPreset } from '../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../config/defaults/provider-defaults'

export type MCPTransportType = 'stdio' | 'sse' | 'http'

export interface MCPServerConfig {
  name: string
  type: MCPTransportType
  command?: string
  args?: string[]
  url?: string
  cwd?: string
  env?: Record<string, string>
  headers?: Record<string, string>
  timeout?: number
  includeTools?: string[]
  excludeTools?: string[]
  trust?: 'full' | 'sandbox' | 'none'
}

export type PermissionMode = 'ask' | 'auto-approve' | 'bypass'

export interface UISettings {
  showBottomPanel: boolean
  showAgentActivity: boolean
  compactMessages: boolean
  showInfoBar: boolean
  showTokenCount: boolean
  showModelInTitleBar: boolean
  hideThinking: boolean
  sidebarOrder: string[]
}

export type AccentColor = 'violet' | 'blue' | 'green' | 'rose' | 'amber' | 'cyan' | 'custom'
export type MonoFont = 'default' | 'jetbrains' | 'fira'
export type SansFont = 'default' | 'inter' | 'outfit' | 'nunito'
export type BorderRadius = 'sharp' | 'default' | 'rounded' | 'pill'
export type UIDensity = 'compact' | 'default' | 'comfortable'
export type CodeTheme =
  | 'default'
  | 'github-dark'
  | 'monokai'
  | 'nord'
  | 'solarized-dark'
  | 'catppuccin'
export type DarkStyle = 'dark' | 'midnight' | 'charcoal'

export interface AppearanceSettings {
  uiScale: number // 0.85 – 1.2, default 1.0 (maps to html font-size: 16px * scale)
  accentColor: AccentColor
  customAccentColor: string // hex, default '#8b5cf6'
  fontMono: MonoFont
  fontSans: SansFont
  fontLigatures: boolean
  chatFontSize: number // 11–20, default 13 (px, independent of uiScale)
  borderRadius: BorderRadius
  density: UIDensity
  codeTheme: CodeTheme
  darkStyle: DarkStyle
  highContrast: boolean
  reduceMotion: boolean
}

export type SendKey = 'enter' | 'ctrl+enter'

export type ReasoningEffort =
  | 'off'
  | 'none'
  | 'minimal'
  | 'low'
  | 'medium'
  | 'high'
  | 'xhigh'
  | 'max'

export interface GenerationSettings {
  maxTokens: number // 256–32000, default 4096
  temperature: number // 0.0–2.0, default 0.7
  topP: number // 0.0–1.0, default 1.0
  customInstructions: string // prepended as system message
  weakModel: string // cheaper model for secondary tasks ('' = use default)
  editorModel: string // cheaper model for file edits by Junior Devs ('' = use primary)
  thinkingEnabled: boolean // Enable extended thinking / reasoning mode (derived from reasoningEffort)
  reasoningEffort: ReasoningEffort // Reasoning intensity: off, low, medium, high
  compactionThreshold: number // 50–95, default 80
  delegationEnabled: boolean // Enable team delegation (Praxis hierarchy) — context % to trigger compaction
}

export interface AgentLimitSettings {
  agentMaxTurns: number // 1–100, default 20
  agentMaxTimeMinutes: number // 1–60, default 10
  autoFixLint: boolean // Run linter after file edits, append errors to tool result
}

export interface BehaviorSettings {
  sendKey: SendKey
  sessionAutoTitle: boolean
  autoScroll: boolean
  lineNumbers: boolean
  wordWrap: boolean
  fileWatcher: boolean // Watch project files for AI comments (// AI!, // AI?)
  voiceDeviceId: string // Preferred audio input device ('' = system default)
  clipboardWatcher: boolean // Detect clipboard changes with code or LLM output
}

export interface NotificationSettings {
  notifyOnCompletion: boolean
  soundOnCompletion: boolean
  soundVolume: number // 0–100, default 50
}

export interface GitSettings {
  enabled: boolean // Enable git integration (auto-detect repos)
  autoCommit: boolean // Auto-commit after successful AI edits
  commitPrefix: string // Commit message prefix, default '[ava]'
}

export interface ToolApprovalRule {
  tool: string // tool name or glob: 'bash', 'write_*', '*'
  action: 'allow' | 'ask' | 'deny'
  paths?: string[] // optional path restriction globs
  reason?: string
}

export type SkillActivationMode = 'auto' | 'agent' | 'always' | 'manual'

export interface CustomSkill {
  id: string
  name: string
  description: string
  fileGlobs: string[]
  instructions: string
  activation?: SkillActivationMode
}

export type RuleActivationMode = 'always' | 'auto' | 'manual'

export interface CustomRule {
  id: string
  name: string
  description: string
  globs: string[]
  activation: RuleActivationMode
  content: string
  enabled: boolean
}

/** @deprecated Use CustomSkill instead */
export type CustomMicroagent = CustomSkill

export interface TrustedFoldersSettings {
  allowed: string[]
  denied: string[]
}

/** Which backend stack to use for the agent loop */
export type AgentBackend = 'core' | 'core-v2'

export interface AppSettings {
  onboardingComplete: boolean
  theme: string
  mode: 'light' | 'dark' | 'system'
  providers: LLMProviderConfig[]
  agents: AgentPreset[]
  autoApprovedTools: string[]
  toolRules: ToolApprovalRule[]
  ui: UISettings
  appearance: AppearanceSettings
  generation: GenerationSettings
  agentLimits: AgentLimitSettings
  behavior: BehaviorSettings
  notifications: NotificationSettings
  git: GitSettings
  permissionMode: PermissionMode
  mcpServers: MCPServerConfig[]
  modelAliases: Record<string, string>
  devMode: boolean
  enabledSkills: string[]
  customSkills: CustomSkill[]
  customRules: CustomRule[]
  hiddenBuiltInSkills: string[]
  trustedFolders: TrustedFoldersSettings
  /** Which backend stack powers the agent loop (default: 'core-v2') */
  agentBackend: AgentBackend
}
