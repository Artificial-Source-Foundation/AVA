/**
 * Extension System
 * Plugin architecture for bundling capabilities.
 */

// Manager
export {
  ExtensionManager,
  getExtensionManager,
  resetExtensionManager,
  setExtensionManager,
} from './manager.js'

// Manifest
export {
  CONFIG_FILENAME,
  DEFAULT_CONTEXT_FILES,
  getContextFilePaths,
  INSTALL_METADATA_FILENAME,
  loadExtensionConfig,
  loadExtensionConfigSync,
  loadInstallMetadata,
  validateExtensionConfig,
  validateExtensionName,
} from './manifest.js'

// Storage
export {
  ExtensionStorage,
  loadEnablement,
  saveEnablement,
} from './storage.js'
// Types
export type {
  EnablementData,
  Extension,
  ExtensionConfig,
  ExtensionEvent,
  ExtensionEventListener,
  ExtensionManagerOptions,
  InstallMetadata,
  InstallOptions,
  InstallType,
  MCPServerExtConfig,
} from './types.js'
