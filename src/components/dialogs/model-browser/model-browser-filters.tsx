/**
 * Model Browser Filters
 *
 * Search bar, provider dropdown, capability filter pills, and sort selector.
 * macOS-inspired styling with subtle glass backgrounds.
 */

import { Search } from 'lucide-solid'
import { type Accessor, type Component, For } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import type { FilterState, ModelCapability, SortOption } from './model-browser-types'

interface ModelBrowserFiltersProps {
  filters: FilterState
  providers: Accessor<LLMProviderConfig[]>
  onSearchChange: (value: string) => void
  onProviderChange: (providerId: string | null) => void
  onCapabilityToggle: (cap: ModelCapability) => void
  onSortChange: (sort: SortOption) => void
  searchInputRef?: (el: HTMLInputElement) => void
}

const ALL_CAPABILITIES: { id: ModelCapability; label: string }[] = [
  { id: 'reasoning', label: 'Reasoning' },
  { id: 'thinking', label: 'Thinking' },
  { id: 'tools', label: 'Tools' },
  { id: 'vision', label: 'Vision' },
  { id: 'free', label: 'Free' },
]

const SORT_OPTIONS: { id: SortOption; label: string }[] = [
  { id: 'name', label: 'Name' },
  { id: 'context', label: 'Context' },
  { id: 'price', label: 'Price' },
]

export const ModelBrowserFilters: Component<ModelBrowserFiltersProps> = (props) => (
  <div class="space-y-3 py-3">
    {/* Search + Provider row */}
    <div class="flex items-center gap-2">
      <div class="relative flex-1">
        <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[#8E8E93]" />
        <input
          type="text"
          ref={props.searchInputRef}
          value={props.filters.search}
          onInput={(e) => props.onSearchChange(e.currentTarget.value)}
          placeholder="Search models..."
          aria-label="Search models"
          class="w-full pl-10 pr-3 py-2 text-[13px] bg-[rgba(255,255,255,0.04)] text-[#F5F5F7] placeholder:text-[#8E8E93] border border-[rgba(255,255,255,0.06)] rounded-[8px] focus:outline-none focus:border-[var(--accent)] transition-colors duration-150"
        />
      </div>
      <select
        value={props.filters.provider ?? ''}
        onChange={(e) => props.onProviderChange(e.currentTarget.value || null)}
        aria-label="Filter by provider"
        class="px-3 py-2 text-[13px] bg-[rgba(255,255,255,0.04)] text-[#F5F5F7] border border-[rgba(255,255,255,0.06)] rounded-[8px] focus:outline-none focus:border-[var(--accent)] transition-colors duration-150 appearance-none cursor-pointer"
        style={{
          'background-image': `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%238E8E93' stroke-width='2'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E")`,
          'background-repeat': 'no-repeat',
          'background-position': 'right 10px center',
          'padding-right': '28px',
        }}
      >
        <option value="">All Providers</option>
        <For each={props.providers()}>{(p) => <option value={p.id}>{p.name}</option>}</For>
      </select>
    </div>

    {/* Capability pills + sort */}
    <div class="flex items-center justify-between">
      <div class="flex items-center gap-1.5">
        <For each={ALL_CAPABILITIES}>
          {(cap) => {
            const isActive = () => props.filters.capabilities.includes(cap.id)
            return (
              <button
                type="button"
                onClick={() => props.onCapabilityToggle(cap.id)}
                aria-pressed={isActive()}
                aria-label={`Filter by ${cap.label}`}
                class={`
                  px-2.5 py-1 text-[11px] font-medium rounded-full
                  transition-all duration-150 cursor-pointer
                  ${
                    isActive()
                      ? 'bg-[var(--accent)] text-white shadow-[0_0_8px_rgba(10,132,255,0.3)]'
                      : 'bg-[rgba(255,255,255,0.04)] text-[#8E8E93] hover:text-[#C8C8CC] hover:bg-[rgba(255,255,255,0.06)]'
                  }
                `}
              >
                {cap.label}
              </button>
            )
          }}
        </For>
      </div>
      <div class="flex items-center gap-0.5 bg-[rgba(255,255,255,0.04)] rounded-[8px] p-0.5">
        <For each={SORT_OPTIONS}>
          {(opt) => (
            <button
              type="button"
              onClick={() => props.onSortChange(opt.id)}
              aria-pressed={props.filters.sort === opt.id}
              aria-label={`Sort by ${opt.label}`}
              class={`
                px-2.5 py-1 text-[11px] font-medium rounded-[6px] transition-all duration-150 cursor-pointer
                ${
                  props.filters.sort === opt.id
                    ? 'text-[#F5F5F7] bg-[rgba(255,255,255,0.08)]'
                    : 'text-[#8E8E93] hover:text-[#C8C8CC]'
                }
              `}
            >
              {opt.label}
            </button>
          )}
        </For>
      </div>
    </div>
  </div>
)
