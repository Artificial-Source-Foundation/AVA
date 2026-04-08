/**
 * Settings Defaults
 * Default values for all settings sub-objects and the top-level AppSettings.
 * Pure data — no side effects, no signals.
 */

import { defaultAgentPresets } from '../../config/defaults/agent-defaults'
import { defaultProviders } from '../../config/defaults/provider-defaults'
import type {
  AgentLimitSettings,
  AppearanceSettings,
  AppSettings,
  BehaviorSettings,
  GenerationSettings,
  GitSettings,
  NotificationSettings,
  UISettings,
} from './settings-types'

export const DEFAULT_UI: UISettings = {
  showBottomPanel: true,
  showAgentActivity: true,
  compactMessages: false,
  showInfoBar: true,
  showTokenCount: true,
  showModelInTitleBar: true,
  hideThinking: false,
  sidebarOrder: ['sessions', 'explorer'],
  toolResponseStyle: 'concise',
}

export const DEFAULT_APPEARANCE: AppearanceSettings = {
  uiScale: 1.0,
  accentColor: 'blue',
  customAccentColor: '#0A84FF',
  fontMono: 'default',
  fontSans: 'default',
  fontLigatures: true,
  fontSize: 'medium',
  chatFontSize: 13,
  borderRadius: 'default',
  density: 'default',
  codeTheme: 'default',
  darkStyle: 'dark',
  thinkingDisplay: 'bubble',
  activityDisplay: 'collapsed',
  highContrast: false,
  reduceMotion: false,
}

export const DEFAULT_GENERATION: GenerationSettings = {
  maxTokens: 4096,
  temperature: 0.7,
  topP: 1.0,
  customInstructions: '',
  weakModel: '',
  editorModel: '',
  compactionModel: '',
  thinkingEnabled: false,
  reasoningEffort: 'off',
  autoCompact: true,
  compactionThreshold: 80,
}

export const DEFAULT_AGENT_LIMITS: AgentLimitSettings = {
  agentMaxTurns: 20,
  agentMaxTimeMinutes: 10,
  autoFixLint: true,
}

export const DEFAULT_BEHAVIOR: BehaviorSettings = {
  sendKey: 'enter',
  sessionAutoTitle: true,
  autoScroll: true,
  lineNumbers: true,
  wordWrap: false,
  fileWatcher: false,
  voiceDeviceId: '',
  clipboardWatcher: false,
  updateCheck: true,
}

export const DEFAULT_NOTIFICATIONS: NotificationSettings = {
  notifyOnCompletion: true,
  soundOnCompletion: false,
  soundVolume: 50,
}

export const DEFAULT_GIT: GitSettings = {
  enabled: true,
  autoCommit: false,
  commitPrefix: '[ava]',
}

export const DEFAULT_SETTINGS: AppSettings = {
  onboardingComplete: false,
  theme: 'glass',
  mode: 'dark',
  providers: defaultProviders,
  agents: defaultAgentPresets,
  autoApprovedTools: [],
  toolRules: [],
  ui: { ...DEFAULT_UI },
  appearance: { ...DEFAULT_APPEARANCE },
  generation: { ...DEFAULT_GENERATION },
  agentLimits: { ...DEFAULT_AGENT_LIMITS },
  behavior: { ...DEFAULT_BEHAVIOR },
  notifications: { ...DEFAULT_NOTIFICATIONS },
  git: { ...DEFAULT_GIT },
  permissionMode: 'ask',
  mcpServers: [],
  modelAliases: {},
  devMode: false,
  logLevel: 'info',
  enabledSkills: [],
  customSkills: [],
  customRules: [],
  hiddenBuiltInSkills: [],
  trustedFolders: { allowed: [], denied: [] },
}
