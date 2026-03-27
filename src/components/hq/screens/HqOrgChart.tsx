import { Maximize2, Network, ZoomIn, ZoomOut } from 'lucide-solid'
import { type Component, createMemo, createSignal, For } from 'solid-js'
import { useHq } from '../../../stores/hq'

const NODE_W = 180
const NODE_H = 100

const TIER_Y: Record<string, number> = {
  director: 40,
  lead: 200,
  worker: 360,
  scout: 360,
}

function statusColor(status: string): string {
  if (status === 'running') return '#06b6d4'
  if (status === 'active') return 'var(--success)'
  if (status === 'idle') return 'var(--text-muted)'
  if (status === 'paused') return 'var(--warning)'
  return 'var(--error)'
}

const HqOrgChart: Component = () => {
  const { agents, navigateToAgent } = useHq()
  const [zoom, setZoom] = createSignal(1)

  const positioned = createMemo(() => {
    const all = agents()
    const tiers: Record<string, typeof all> = { director: [], lead: [], worker: [], scout: [] }
    for (const a of all) tiers[a.tier]?.push(a)

    const nodes: { agent: (typeof all)[0]; x: number; y: number }[] = []
    for (const tier of ['director', 'lead', 'worker', 'scout'] as const) {
      const list = tiers[tier]
      const totalW = list.length * NODE_W + (list.length - 1) * 40
      const startX = Math.max(40, (900 - totalW) / 2)
      list.forEach((a, i) => {
        nodes.push({ agent: a, x: startX + i * (NODE_W + 40), y: TIER_Y[tier] })
      })
    }
    return nodes
  })

  const connectors = createMemo(() => {
    const nodes = positioned()
    const lines: { x1: number; y1: number; x2: number; y2: number }[] = []
    for (const node of nodes) {
      if (node.agent.parentId) {
        const parent = nodes.find((n) => n.agent.id === node.agent.parentId)
        if (parent) {
          const px = parent.x + NODE_W / 2
          const py = parent.y + NODE_H
          const cx = node.x + NODE_W / 2
          const cy = node.y
          const midY = (py + cy) / 2
          lines.push(
            { x1: px, y1: py, x2: px, y2: midY },
            { x1: px, y1: midY, x2: cx, y2: midY },
            { x1: cx, y1: midY, x2: cx, y2: cy }
          )
        }
      }
    }
    return lines
  })

  return (
    <div class="flex flex-col h-full" style={{ 'background-color': 'var(--background)' }}>
      {/* Header */}
      <div
        class="flex items-center justify-between shrink-0 px-6 h-14"
        style={{ 'border-bottom': '1px solid var(--border-subtle)' }}
      >
        <div class="flex items-center gap-2.5">
          <Network size={18} style={{ color: 'var(--text-muted)' }} />
          <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
            Org Chart
          </span>
        </div>
        <div class="flex items-center gap-1">
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
            }}
            onClick={() => setZoom((z) => Math.min(z + 0.1, 2))}
            title="Zoom In"
          >
            <ZoomIn size={14} style={{ color: 'var(--text-secondary)' }} />
          </button>
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
            }}
            onClick={() => setZoom((z) => Math.max(z - 0.1, 0.5))}
            title="Zoom Out"
          >
            <ZoomOut size={14} style={{ color: 'var(--text-secondary)' }} />
          </button>
          <button
            type="button"
            class="flex items-center justify-center w-7 h-7 rounded-md"
            style={{
              'background-color': 'var(--surface)',
              border: '1px solid var(--border-subtle)',
            }}
            onClick={() => setZoom(1)}
            title="Fit"
          >
            <Maximize2 size={14} style={{ color: 'var(--text-secondary)' }} />
          </button>
        </div>
      </div>

      {/* SVG Canvas */}
      <div class="flex-1 overflow-auto">
        <svg
          aria-label="HQ organization chart"
          role="img"
          width="900"
          height="500"
          style={{ transform: `scale(${zoom()})`, 'transform-origin': 'top left' }}
        >
          <title>HQ organization chart</title>
          {/* Connector lines */}
          <For each={connectors()}>
            {(line) => (
              <line
                x1={line.x1}
                y1={line.y1}
                x2={line.x2}
                y2={line.y2}
                stroke="var(--border-subtle)"
                stroke-width="1.5"
              />
            )}
          </For>

          {/* Nodes */}
          <For each={positioned()}>
            {(node) => (
              // biome-ignore lint/a11y/useSemanticElements: svg groups need keyboard-accessible interaction for node navigation
              <g
                role="button"
                tabIndex={0}
                style={{ cursor: 'pointer' }}
                onClick={() => navigateToAgent(node.agent.id)}
                onKeyDown={(event) => {
                  if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault()
                    navigateToAgent(node.agent.id)
                  }
                }}
              >
                <rect
                  x={node.x}
                  y={node.y}
                  width={NODE_W}
                  height={NODE_H}
                  rx={10}
                  ry={10}
                  fill="var(--surface)"
                  stroke="var(--border-subtle)"
                  stroke-width="1"
                />
                {/* Status dot */}
                <circle
                  cx={node.x + 16}
                  cy={node.y + 20}
                  r={4}
                  fill={statusColor(node.agent.status)}
                />
                {/* Name */}
                <text
                  x={node.x + 28}
                  y={node.y + 24}
                  fill="var(--text-primary)"
                  font-size="13"
                  font-weight="600"
                >
                  {node.agent.name}
                </text>
                {/* Role */}
                <text x={node.x + 16} y={node.y + 46} fill="var(--text-muted)" font-size="10">
                  {node.agent.role}
                </text>
                {/* Model */}
                <text
                  x={node.x + 16}
                  y={node.y + 64}
                  fill="var(--text-muted)"
                  font-size="10"
                  font-family="monospace"
                >
                  {node.agent.model}
                </text>
                {/* Status label */}
                <text
                  x={node.x + 16}
                  y={node.y + 84}
                  fill={statusColor(node.agent.status)}
                  font-size="9"
                  font-weight="600"
                >
                  {node.agent.status}
                </text>
              </g>
            )}
          </For>
        </svg>
      </div>
    </div>
  )
}

export default HqOrgChart
