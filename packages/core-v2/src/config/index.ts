export {
  getSettingsManager,
  resetSettingsManager,
  SettingsManager,
  setSettingsManager,
} from './manager.js'

export type {
  AgentSettings,
  CoreSettings,
  ProviderSettings,
  SettingsEvent,
  SettingsEventListener,
} from './types.js'

export {
  DEFAULT_AGENT_SETTINGS,
  DEFAULT_PROVIDER_SETTINGS,
} from './types.js'
