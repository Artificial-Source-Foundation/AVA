/**
 * Permissions & Trust Tab — Merged view of permissions and trusted folders.
 *
 * Shows permission mode + tool rules first, then trusted folders section.
 */

import type { Component } from 'solid-js'
import { PermissionsTab } from './PermissionsTab'
import { TrustedFoldersTab } from './TrustedFoldersTab'

export const PermissionsAndTrustTab: Component = () => {
  return (
    <div class="space-y-6">
      {/* Permissions section */}
      <PermissionsTab />

      {/* Divider */}
      <div class="border-t border-[var(--border-subtle)]" />

      {/* Trusted Folders section */}
      <TrustedFoldersTab />
    </div>
  )
}
