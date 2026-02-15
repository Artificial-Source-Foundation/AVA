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
}

export const DEFAULT_APPEARANCE: AppearanceSettings = {
  uiScale: 1.0,
  accentColor: 'violet',
  customAccentColor: '#8b5cf6',
  fontMono: 'default',
  fontSans: 'default',
  fontLigatures: true,
  chatFontSize: 13,
  borderRadius: 'default',
  density: 'default',
  codeTheme: 'default',
  darkStyle: 'dark',
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
  ui: { ...DEFAULT_UI },
  appearance: { ...DEFAULT_APPEARANCE },
  generation: { ...DEFAULT_GENERATION },
  agentLimits: { ...DEFAULT_AGENT_LIMITS },
  behavior: { ...DEFAULT_BEHAVIOR },
  notifications: { ...DEFAULT_NOTIFICATIONS },
  git: { ...DEFAULT_GIT },
  permissionMode: 'ask',
  mcpServers: [],
  devMode: false,
}
