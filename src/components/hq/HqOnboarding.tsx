import { ArrowRight, Building2, Check } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useHq } from '../../stores/hq'

interface ModelOption {
  id: string
  name: string
  description: string
  recommended?: boolean
}

const DIRECTOR_MODELS: ModelOption[] = [
  {
    id: 'opus',
    name: 'Claude Opus (Anthropic)',
    description: 'Best reasoning · Recommended for Director',
    recommended: true,
  },
  { id: 'gpt54', name: 'GPT-5.4 (OpenAI)', description: 'Strong reasoning · Good alternative' },
  { id: 'gemini', name: 'Gemini Pro (Google)', description: 'Large context · Fast planning' },
]

const STEPS = ['Director Model', 'Team Config', 'Review']

export const HqOnboarding: Component<{ onComplete: () => void }> = (props) => {
  const { markOnboarded } = useHq()
  const [step, setStep] = createSignal(0)
  const [selectedModel, setSelectedModel] = createSignal('opus')
  const [leadsEnabled, setLeadsEnabled] = createSignal({ cto: true, qa: true })

  const handleComplete = (): void => {
    markOnboarded()
    props.onComplete()
  }

  return (
    <div
      class="absolute inset-0 flex items-center justify-center z-50"
      style={{ 'background-color': 'rgba(0,0,0,0.6)' }}
    >
      <div
        class="flex flex-col gap-8 w-[560px] p-10 rounded-xl"
        style={{
          'background-color': 'var(--surface)',
          border: '1px solid var(--border-subtle)',
        }}
      >
        {/* Header */}
        <div class="flex flex-col items-center gap-2.5">
          <div
            class="flex items-center justify-center w-14 h-14 rounded-full"
            style={{ 'background-color': 'var(--accent-subtle)' }}
          >
            <Building2 size={28} class="text-violet-400" />
          </div>
          <span class="text-[22px] font-bold" style={{ color: 'var(--text-primary)' }}>
            Set up AVA HQ
          </span>
          <span
            class="text-sm text-center max-w-[400px]"
            style={{ color: 'var(--text-muted)', 'line-height': '1.5' }}
          >
            Configure your multi-agent team for this project. You can change these settings anytime.
          </span>
        </div>

        {/* Step Indicators */}
        <div class="flex items-center justify-center gap-2">
          <For each={STEPS}>
            {(_label, i) => (
              <>
                <Show when={i() > 0}>
                  <div
                    class="w-10 h-0.5 rounded-full"
                    style={{
                      'background-color': i() <= step() ? 'var(--accent)' : 'var(--border-default)',
                    }}
                  />
                </Show>
                <div
                  class="flex items-center justify-center w-6 h-6 rounded-full text-[10px] font-bold"
                  style={{
                    'background-color': i() <= step() ? 'var(--accent)' : 'var(--border-default)',
                    color: i() <= step() ? 'white' : 'var(--text-muted)',
                  }}
                >
                  {i() < step() ? <Check size={12} /> : i() + 1}
                </div>
              </>
            )}
          </For>
        </div>

        {/* Step Content */}
        <Show when={step() === 0}>
          <StepDirectorModel selected={selectedModel()} onSelect={setSelectedModel} />
        </Show>
        <Show when={step() === 1}>
          <StepTeamConfig
            leads={leadsEnabled()}
            onToggle={(key) =>
              setLeadsEnabled((prev) => ({ ...prev, [key]: !prev[key as keyof typeof prev] }))
            }
          />
        </Show>
        <Show when={step() === 2}>
          <StepReview model={selectedModel()} leads={leadsEnabled()} />
        </Show>

        {/* Actions */}
        <div class="flex items-center justify-between">
          <button
            type="button"
            class="text-xs"
            style={{ color: 'var(--text-muted)' }}
            onClick={handleComplete}
          >
            Skip for now
          </button>
          <Show
            when={step() < 2}
            fallback={
              <button
                type="button"
                class="flex items-center gap-1.5 h-9 px-5 rounded-lg text-xs font-semibold"
                style={{ 'background-color': 'var(--success)', color: 'white' }}
                onClick={handleComplete}
              >
                Launch HQ
              </button>
            }
          >
            <button
              type="button"
              class="flex items-center gap-1.5 h-9 px-5 rounded-lg text-xs font-semibold"
              style={{ 'background-color': 'var(--accent)', color: 'white' }}
              onClick={() => setStep((s) => s + 1)}
            >
              Next: {STEPS[step() + 1]}
              <ArrowRight size={14} />
            </button>
          </Show>
        </div>
      </div>
    </div>
  )
}

// ── Step Components ──────────────────────────────────────────────────

const StepDirectorModel: Component<{ selected: string; onSelect: (id: string) => void }> = (
  props
) => (
  <div class="flex flex-col gap-4">
    <div>
      <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
        Choose your Director model
      </span>
      <p class="text-xs mt-1" style={{ color: 'var(--text-muted)', 'line-height': '1.5' }}>
        The Director analyzes goals, creates plans, and supervises agents. Pick the smartest model
        you have access to.
      </p>
    </div>
    <div class="flex flex-col gap-1.5">
      <For each={DIRECTOR_MODELS}>
        {(model) => (
          <button
            type="button"
            class="flex items-center gap-3 w-full h-12 px-3.5 rounded-lg text-left transition-colors"
            style={{
              'background-color':
                props.selected === model.id ? 'var(--accent-subtle)' : 'rgba(255,255,255,0.02)',
              border: `1px solid ${
                props.selected === model.id ? 'var(--accent-border)' : 'var(--border-subtle)'
              }`,
            }}
            onClick={() => props.onSelect(model.id)}
          >
            <div
              class="w-4 h-4 rounded-full border-4"
              style={{
                'background-color': props.selected === model.id ? 'var(--accent)' : 'transparent',
                'border-color':
                  props.selected === model.id ? 'var(--surface)' : 'var(--border-default)',
              }}
            />
            <div class="flex flex-col flex-1 gap-0.5">
              <span
                class="text-xs font-semibold"
                style={{
                  color:
                    props.selected === model.id ? 'var(--text-primary)' : 'var(--text-secondary)',
                }}
              >
                {model.name}
              </span>
              <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
                {model.description}
              </span>
            </div>
            <Show when={model.recommended}>
              <span
                class="text-[9px] font-semibold px-2 py-0.5 rounded"
                style={{
                  color: 'var(--success)',
                  'background-color': 'rgba(34,197,94,0.1)',
                }}
              >
                recommended
              </span>
            </Show>
          </button>
        )}
      </For>
    </div>
  </div>
)

const StepTeamConfig: Component<{
  leads: Record<string, boolean>
  onToggle: (key: string) => void
}> = (props) => (
  <div class="flex flex-col gap-4">
    <div>
      <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
        Configure your team
      </span>
      <p class="text-xs mt-1" style={{ color: 'var(--text-muted)', 'line-height': '1.5' }}>
        Enable the leads you need. Workers are assigned automatically.
      </p>
    </div>
    <div class="flex flex-col gap-2">
      <ToggleRow
        label="CTO (Backend Lead)"
        sub="Handles core implementation"
        enabled={props.leads.cto}
        onToggle={() => props.onToggle('cto')}
      />
      <ToggleRow
        label="QA Lead"
        sub="Code review and testing"
        enabled={props.leads.qa}
        onToggle={() => props.onToggle('qa')}
      />
    </div>
  </div>
)

const ToggleRow: Component<{
  label: string
  sub: string
  enabled: boolean
  onToggle: () => void
}> = (props) => (
  <div
    class="flex items-center justify-between px-3.5 h-12 rounded-lg"
    style={{
      'background-color': 'rgba(255,255,255,0.02)',
      border: '1px solid var(--border-subtle)',
    }}
  >
    <div class="flex flex-col gap-0.5">
      <span class="text-xs font-medium" style={{ color: 'var(--text-primary)' }}>
        {props.label}
      </span>
      <span class="text-[10px]" style={{ color: 'var(--text-muted)' }}>
        {props.sub}
      </span>
    </div>
    <button
      type="button"
      class="w-9 h-5 rounded-full transition-colors relative"
      style={{
        'background-color': props.enabled ? 'var(--accent)' : 'var(--border-default)',
      }}
      onClick={props.onToggle}
    >
      <div
        class="absolute top-0.5 w-4 h-4 rounded-full bg-white transition-transform"
        style={{ left: props.enabled ? '18px' : '2px' }}
      />
    </button>
  </div>
)

const StepReview: Component<{ model: string; leads: Record<string, boolean> }> = (props) => {
  const modelName = () => DIRECTOR_MODELS.find((m) => m.id === props.model)?.name ?? props.model

  return (
    <div class="flex flex-col gap-4">
      <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
        Review your setup
      </span>
      <div class="flex flex-col gap-2">
        <ReviewRow label="Director" value={modelName()} />
        <ReviewRow label="CTO" value={props.leads.cto ? 'Enabled' : 'Disabled'} />
        <ReviewRow label="QA Lead" value={props.leads.qa ? 'Enabled' : 'Disabled'} />
        <ReviewRow label="Workers" value="Auto-assigned (Sonnet tier)" />
        <ReviewRow label="Scouts" value="Auto (Haiku/Flash tier)" />
      </div>
    </div>
  )
}

const ReviewRow: Component<{ label: string; value: string }> = (props) => (
  <div
    class="flex items-center justify-between px-3.5 h-10 rounded-lg"
    style={{
      'background-color': 'rgba(255,255,255,0.02)',
      border: '1px solid var(--border-subtle)',
    }}
  >
    <span class="text-xs" style={{ color: 'var(--text-muted)' }}>
      {props.label}
    </span>
    <span class="text-xs font-medium" style={{ color: 'var(--text-primary)' }}>
      {props.value}
    </span>
  </div>
)
