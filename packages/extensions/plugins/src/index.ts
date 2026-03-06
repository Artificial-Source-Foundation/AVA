/**
 * Plugin infrastructure extension -- install/uninstall backend and registry API.
 *
 * Provides the installer and catalog modules for managing community plugins.
 * No tools registered -- CLI commands use installer/catalog directly.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { activateToolHooks, pluginToolHooksApi } from './tool-hooks.js'

export function activate(api: ExtensionAPI): Disposable {
  api.log.info('Plugin infrastructure loaded')
  const toolHooksDisposable = activateToolHooks(api)

  return {
    dispose() {
      toolHooksDisposable.dispose()
    },
  }
}

export { pluginToolHooksApi }

export type {
  CatalogEntry,
  CatalogFilterOptions,
  CatalogSearchResult,
  CatalogSortBy,
} from './catalog.js'
export {
  clearCatalogCache,
  fetchCatalog,
  filterCatalog,
  getCatalogEntry,
  searchCatalog,
  sortCatalog,
} from './catalog.js'
export type { InstalledPlugin, InstallResult } from './installer.js'
export { getInstalledPlugins, installPlugin, uninstallPlugin } from './installer.js'
export type { PluginReview, PluginReviewInput } from './reviews.js'
export { ReviewStore } from './reviews.js'
export type { ToolDescribeHook } from './tool-hooks.js'
