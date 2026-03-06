/**
 * Plugin Tab Components – barrel export
 */

export type { FeaturedPluginCardProps, PluginCardProps } from './PluginCard'
export { FeaturedPluginCard, PluginCard } from './PluginCard'
export type { PluginPermissionBadgesProps, PluginSourceInfoProps } from './PluginDetailPanels'
export { PluginPermissionBadges, PluginSourceInfo } from './PluginDetailPanels'
export type { DevModeStatus, PluginDevModeProps } from './PluginDevMode'
export { PluginDevMode } from './PluginDevMode'
export type {
  GitInstallDialogProps,
  LinkLocalDialogProps,
  PermissionConfirmDialogProps,
} from './PluginInstallDialog'
export {
  GitInstallDialog,
  LinkLocalDialog,
  PermissionConfirmDialog,
} from './PluginInstallDialog'
export type { PluginSearchProps } from './PluginSearch'
export { PluginSearch } from './PluginSearch'
export type { PluginToolbarProps } from './PluginToolbar'
export { PluginToolbar } from './PluginToolbar'

export {
  categoryLabel,
  formatDownloads,
  formatSyncTime,
  permissionColor,
  sourceLabel,
} from './plugin-utils'
