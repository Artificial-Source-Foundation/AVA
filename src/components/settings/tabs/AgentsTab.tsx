/**
 * Agents Settings Tab — Obsidian-Style Split Panel
 *
 * Left panel: tier-grouped list with search and toggles.
 * Right panel: inline detail/edit form (replaces the old AgentEditModal).
 * Fully self-contained — manages its own state via useSettings().
 */

import { Users, Wand2 } from 'lucide-solid'
import { type Component, createSignal } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'
import { resolveAgentIcon } from '../../../config/defaults/agent-defaults'
import { useSettings } from '../../../stores/settings'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { AgentsTabDetail } from './agents-tab-detail'
import { AgentsTabList } from './agents-tab-list'

// Re-export so existing barrel consumers stay happy
export type { AgentPreset } from '../../../config/defaults/agent-defaults'
export { defaultAgentPresets } from '../../../config/defaults/agent-defaults'

export const AgentsTab: Component = () => {
  const { settings, updateAgent, addAgent, removeAgent, exportAgents, importAgents } = useSettings()

  const [selectedId, setSelectedId] = createSignal<string | null>(null)
  const [searchQuery, setSearchQuery] = createSignal('')
  const [creatingNew, setCreatingNew] = createSignal(false)

  const selectedAgent = () => {
    if (creatingNew()) {
      return {
        id: `custom-${Date.now()}`,
        name: '',
        description: '',
        icon: Wand2,
        enabled: true,
        capabilities: [],
        isCustom: true,
        tier: 'worker' as const,
        tools: [],
      }
    }
    const id = selectedId()
    return id ? (settings().agents.find((a) => a.id === id) ?? null) : null
  }

  const handleToggle = (id: string, enabled: boolean) => updateAgent(id, { enabled })

  const handleSave = (agent: AgentPreset) => {
    if (creatingNew()) {
      addAgent({ ...agent, icon: resolveAgentIcon(undefined) })
      setCreatingNew(false)
      setSelectedId(agent.id)
    } else {
      updateAgent(agent.id, agent)
    }
  }

  const handleDelete = (id: string) => {
    removeAgent(id)
    if (selectedId() === id) setSelectedId(null)
  }

  const handleCreate = () => {
    setCreatingNew(true)
    setSelectedId(null)
  }

  const handleSelect = (id: string) => {
    setCreatingNew(false)
    setSelectedId(id)
  }

  const handleExport = () => {
    const json = exportAgents()
    const blob = new Blob([json], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = 'ava-agents.json'
    a.click()
    URL.revokeObjectURL(url)
  }

  const handleImport = () => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = '.json'
    input.onchange = () => {
      const file = input.files?.[0]
      if (!file) return
      file.text().then((text) => {
        try {
          importAgents(text)
        } catch {
          // Import failed — silently ignore
        }
      })
    }
    input.click()
  }

  return (
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      <SettingsCard
        icon={Users}
        title="Agent Presets"
        description="Configure agent roles, tools, and system prompts"
      >
        <div
          class="flex -mx-6 -mb-6 rounded-b-[var(--radius-xl)] overflow-hidden"
          style={{ height: '520px' }}
        >
          <div class="w-[40%] flex-shrink-0">
            <AgentsTabList
              agents={settings().agents}
              selectedId={creatingNew() ? null : selectedId()}
              onSelect={handleSelect}
              onToggle={handleToggle}
              searchQuery={searchQuery()}
              onSearchChange={setSearchQuery}
              onImport={handleImport}
              onExport={handleExport}
              onCreate={handleCreate}
            />
          </div>
          <div class="flex-1 min-w-0">
            <AgentsTabDetail
              agent={selectedAgent()}
              providers={settings().providers}
              onSave={handleSave}
              onDelete={handleDelete}
              isCreating={creatingNew()}
            />
          </div>
        </div>
      </SettingsCard>
    </div>
  )
}
