/**
 * Skills & Commands Tab — Merged view of skills/rules and custom commands.
 *
 * Shows Skills section first (with rules), then a divider, then Commands section.
 */

import type { Component } from 'solid-js'
import { CommandsTab } from './CommandsTab'
import { SkillsTab } from './SkillsTab'

export const SkillsAndCommandsTab: Component = () => {
  return (
    <div class="space-y-6">
      {/* Skills & Rules section */}
      <SkillsTab />

      {/* Divider */}
      <div class="border-t border-[var(--border-subtle)]" />

      {/* Commands section */}
      <CommandsTab />
    </div>
  )
}
