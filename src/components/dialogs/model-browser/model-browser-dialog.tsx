/**
 * Model Browser Dialog
 *
 * Full dialog shell for browsing and selecting models.
 * Shows only enabled providers that have models.
 */

import { type Component, createMemo, createSignal } from 'solid-js'
import { Dialog } from '../../ui/Dialog'
import { ModelBrowserFilters } from './model-browser-filters'
import { ModelBrowserGrid } from './model-browser-grid'
import { ModelBrowserHeader } from './model-browser-header'
import { aggregateModels, filterModels, sortModels } from './model-browser-helpers'
import type {
  FilterState,
  ModelBrowserDialogProps,
  ModelCapability,
  SortOption,
} from './model-browser-types'

export const ModelBrowserDialog: Component<ModelBrowserDialogProps> = (props) => {
  const [filters, setFilters] = createSignal<FilterState>({
    search: '',
    provider: null,
    capabilities: [],
    sort: 'name',
  })

  const allModels = createMemo(() => aggregateModels(props.enabledProviders()))

  const filteredModels = createMemo(() => {
    const f = filters()
    return sortModels(filterModels(allModels(), f), f.sort)
  })

  const selectedBrowsable = createMemo(() => {
    const provId = props.selectedProvider?.()
    return (
      allModels().find(
        (m) => m.id === props.selectedModel() && (!provId || m.providerId === provId)
      ) ?? allModels().find((m) => m.id === props.selectedModel())
    )
  })

  const selectedProvider = createMemo(() =>
    props.enabledProviders().find((p) => p.id === selectedBrowsable()?.providerId)
  )

  const handleSelect = (modelId: string, providerId: string) => {
    props.onSelect(modelId, providerId)
    props.onOpenChange(false)
  }

  const updateFilter = <K extends keyof FilterState>(key: K, value: FilterState[K]) => {
    setFilters((prev) => ({ ...prev, [key]: value }))
  }

  const toggleCapability = (cap: ModelCapability) => {
    setFilters((prev) => ({
      ...prev,
      capabilities: prev.capabilities.includes(cap)
        ? prev.capabilities.filter((c) => c !== cap)
        : [...prev.capabilities, cap],
    }))
  }

  return (
    <Dialog open={props.open()} onOpenChange={props.onOpenChange} title="Model Browser" size="2xl">
      <ModelBrowserHeader
        selectedModel={selectedBrowsable()}
        provider={selectedProvider()}
        modelCount={() => filteredModels().length}
      />

      <ModelBrowserFilters
        filters={filters()}
        providers={props.enabledProviders}
        onSearchChange={(v) => updateFilter('search', v)}
        onProviderChange={(v) => updateFilter('provider', v)}
        onCapabilityToggle={toggleCapability}
        onSortChange={(v: SortOption) => updateFilter('sort', v)}
      />

      <ModelBrowserGrid
        models={filteredModels()}
        selectedModelId={props.selectedModel()}
        selectedProviderId={props.selectedProvider?.() ?? undefined}
        providers={props.enabledProviders()}
        onSelect={handleSelect}
      />
    </Dialog>
  )
}
