/**
 * Config Module
 *
 * Application settings management with validation, persistence, and integration.
 *
 * @example
 * ```ts
 * import {
 *   getSettingsManager,
 *   getCredentialsManager,
 *   createAgentConfigFromSettings,
 * } from '@ava/core/config'
 *
 * // Initialize settings
 * const settings = getSettingsManager()
 * await settings.load()
 *
 * // Get settings by category
 * const agentSettings = settings.get('agent')
 *
 * // Update settings
 * settings.set('agent', { maxTurns: 100 })
 * await settings.save()
 *
 * // Manage API keys
 * const credentials = getCredentialsManager()
 * await credentials.setApiKey('anthropic', 'sk-ant-...')
 *
 * // Create agent config from settings
 * const agentConfig = createAgentConfigFromSettings()
 * ```
 */

// Credentials
export {
  CredentialsManager,
  CredentialValidationError,
  createCredentialsManager,
  getCredentialsManager,
  KEY_PATTERNS,
  KNOWN_PROVIDERS,
  PROVIDER_NAMES,
  setCredentialsManager,
} from './credentials.js'
// Export/Import
export {
  backupSettings,
  diffSettings,
  exportFromManager,
  exportSettingsToFile,
  exportSettingsToJson,
  getDefaultSettingsJson,
  type ImportResult,
  importSettingsFromFile,
  importSettingsFromJson,
  importToManager,
  mergeSettings,
  previewImport,
  resetToDefaults,
  type SettingsDiff,
} from './export.js'
// Integration
export {
  applySettingsToAgentConfig,
  applySettingsToContext,
  createAgentConfigFromSettings,
  createContextOptionsFromSettings,
  createSessionConfigFromSettings,
  getEnabledValidators,
  getLLMClientOptions,
  getRequestTimeout,
  initializeSettingsIntegration,
  isPathDenied,
  isValidatorEnabled,
  requiresConfirmation,
  watchAgentSettings,
  watchContextSettings,
} from './integration.js'

// Manager
export {
  createSettingsManager,
  getSettingsManager,
  SettingsManager,
  SettingsValidationError,
  setSettingsManager,
} from './manager.js'
// Migration
export {
  type EnvMigrationReport,
  findEnvApiKeys,
  getChangedFields,
  getCurrentVersion,
  getEnvMigrationReport,
  getLegacyPaths,
  type LegacySettings,
  mergeWithDefaults,
  migrateSettings,
  needsMigration,
  sanitizeForExport,
  validateImportedSettings,
} from './migration.js'
// Schemas
export {
  AgentSettingsSchema,
  ContextSettingsSchema,
  ExportableSettingsSchema,
  LLMProviderSchema,
  MemorySettingsSchema,
  PartialAgentSettingsSchema,
  PartialContextSettingsSchema,
  PartialMemorySettingsSchema,
  PartialPermissionSettingsSchema,
  PartialProviderSettingsSchema,
  PartialUISettingsSchema,
  PermissionSettingsSchema,
  ProviderSettingsSchema,
  SettingsSchema,
  ThemeSchema,
  UISettingsSchema,
  type ValidatedAgentSettings,
  type ValidatedContextSettings,
  type ValidatedMemorySettings,
  type ValidatedPermissionSettings,
  type ValidatedProviderSettings,
  type ValidatedSettings,
  type ValidatedUISettings,
  ValidatorTypeSchema,
} from './schema.js'
// Storage
export {
  deleteSettingsFile,
  ensureSettingsDir,
  getSettingsDir,
  getSettingsPath,
  loadSettingsFromFile,
  saveSettingsToFile,
} from './storage.js'
// Types
export * from './types.js'
