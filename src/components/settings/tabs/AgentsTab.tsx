/**
 * Agents Settings Tab — Split Panel Layout
 *
 * Matches the Pencil design: flat agent list on the left (280px),
 * detail form on the right. No outer card wrapper.
 */

import { Wand2 } from 'lucide-solid'
import { type Component, createSignal } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'
import { resolveAgentIcon } from '../../../config/defaults/agent-defaults'
import { useSettings } from '../../../stores/settings'
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
    <div
      class="flex min-h-0 overflow-hidden"
      style={{
        height: '100%',
        background: '#0A0A0C',
      }}
    >
      {/* Agent List — 280px fixed width */}
      <div style={{ width: '280px', 'flex-shrink': '0', height: '100%' }}>
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
      {/* Agent Detail — fills remaining width */}
      <div class="flex-1 min-w-0 min-h-0 overflow-hidden">
        <AgentsTabDetail
          agent={selectedAgent()}
          providers={settings().providers}
          onSave={handleSave}
          onDelete={handleDelete}
          isCreating={creatingNew()}
        />
      </div>
    </div>
  )
}
