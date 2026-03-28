/**
 * MCP Resource Renderer
 *
 * Renders MCP UIResource responses as interactive widgets.
 * Supports: table, form, chart (bar), image, markdown.
 */

import { type Component, For, Match, Show, Switch } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

export interface MCPUIResource {
  type: 'table' | 'form' | 'chart' | 'image' | 'markdown'
  data: unknown
}

interface TableData {
  headers: string[]
  rows: (string | number)[][]
}

interface FormData {
  fields: { label: string; value: string | number | boolean }[]
}

interface ChartData {
  title?: string
  bars: { label: string; value: number }[]
}

interface ImageData {
  src: string
  alt?: string
}

// ============================================================================
// Sub-components
// ============================================================================

const ResourceTable: Component<{ data: TableData }> = (props) => (
  <div class="overflow-x-auto">
    <table class="w-full text-[12px] border-collapse">
      <thead>
        <tr class="border-b border-[var(--border-default)]">
          <For each={props.data.headers}>
            {(header) => (
              <th class="px-3 py-2 text-left font-semibold text-[var(--text-secondary)] bg-[var(--alpha-white-3)]">
                {header}
              </th>
            )}
          </For>
        </tr>
      </thead>
      <tbody>
        <For each={props.data.rows}>
          {(row, i) => (
            <tr
              class="border-b border-[var(--border-subtle)]"
              classList={{ 'bg-[var(--alpha-white-2)]': i() % 2 === 1 }}
            >
              <For each={row}>
                {(cell) => <td class="px-3 py-1.5 text-[var(--text-primary)]">{String(cell)}</td>}
              </For>
            </tr>
          )}
        </For>
      </tbody>
    </table>
  </div>
)

const ResourceForm: Component<{ data: FormData }> = (props) => (
  <div class="space-y-2 px-3 py-2">
    <For each={props.data.fields}>
      {(field) => (
        <div class="flex items-baseline gap-3">
          <span class="text-[11px] font-medium text-[var(--text-muted)] uppercase tracking-wider min-w-[100px] shrink-0">
            {field.label}
          </span>
          <span class="text-[12px] text-[var(--text-primary)] bg-[var(--surface-sunken)] px-2 py-1 rounded-[var(--radius-sm)] flex-1">
            {String(field.value)}
          </span>
        </div>
      )}
    </For>
  </div>
)

const ResourceChart: Component<{ data: ChartData }> = (props) => {
  const maxValue = () => {
    const vals = props.data.bars.map((b) => b.value)
    return Math.max(...vals, 1)
  }

  return (
    <div class="px-3 py-2 space-y-2">
      <Show when={props.data.title}>
        <div class="text-[12px] font-semibold text-[var(--text-secondary)]">{props.data.title}</div>
      </Show>
      <div class="space-y-1.5">
        <For each={props.data.bars}>
          {(bar) => {
            const pct = () => Math.max((bar.value / maxValue()) * 100, 2)
            return (
              <div class="flex items-center gap-2">
                <span class="text-[11px] text-[var(--text-muted)] min-w-[80px] truncate text-right shrink-0">
                  {bar.label}
                </span>
                <div class="flex-1 h-5 bg-[var(--alpha-white-3)] rounded-[var(--radius-sm)] overflow-hidden">
                  <div
                    class="h-full w-full origin-left bg-[var(--accent)] rounded-[var(--radius-sm)] transition-transform duration-300"
                    style={{ transform: `scaleX(${pct() / 100})` }}
                  />
                </div>
                <span class="text-[11px] text-[var(--text-secondary)] tabular-nums min-w-[40px]">
                  {bar.value}
                </span>
              </div>
            )
          }}
        </For>
      </div>
    </div>
  )
}

const ResourceImage: Component<{ data: ImageData }> = (props) => (
  <div class="px-3 py-2">
    <img
      src={props.data.src}
      alt={props.data.alt ?? 'MCP resource image'}
      class="max-w-full rounded-[var(--radius-md)] border border-[var(--border-subtle)]"
      loading="lazy"
    />
    <Show when={props.data.alt}>
      <p class="text-[11px] text-[var(--text-muted)] mt-1">{props.data.alt}</p>
    </Show>
  </div>
)

const ResourceMarkdown: Component<{ data: string }> = (props) => (
  <div class="px-3 py-2">
    <pre class="text-[12px] text-[var(--text-primary)] font-[var(--font-ui-mono)] whitespace-pre-wrap break-words leading-relaxed">
      {props.data}
    </pre>
  </div>
)

// ============================================================================
// Main Component
// ============================================================================

interface MCPResourceRendererProps {
  resource: MCPUIResource
}

export const MCPResourceRenderer: Component<MCPResourceRendererProps> = (props) => {
  return (
    <div class="overflow-hidden rounded-b-[var(--radius-md)] border-t border-[var(--border-default)] bg-[var(--bg-inset,var(--surface-sunken))]">
      <Switch
        fallback={
          <div class="px-3 py-2 text-[11px] text-[var(--text-muted)]">
            Unsupported resource type: {props.resource.type}
          </div>
        }
      >
        <Match when={props.resource.type === 'table'}>
          <ResourceTable data={props.resource.data as TableData} />
        </Match>
        <Match when={props.resource.type === 'form'}>
          <ResourceForm data={props.resource.data as FormData} />
        </Match>
        <Match when={props.resource.type === 'chart'}>
          <ResourceChart data={props.resource.data as ChartData} />
        </Match>
        <Match when={props.resource.type === 'image'}>
          <ResourceImage data={props.resource.data as ImageData} />
        </Match>
        <Match when={props.resource.type === 'markdown'}>
          <ResourceMarkdown data={props.resource.data as string} />
        </Match>
      </Switch>
    </div>
  )
}
