import { Bug, FlaskConical, Play, Search, Sparkles, Wand2 } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useWorkflows } from '../../../stores/workflows'

const STARTER_TEMPLATES = [
  {
    icon: Search,
    title: 'Explore Codebase',
    prompt:
      'Give me a high-level overview of this project. What are the key files, architecture patterns, and how does everything fit together?',
  },
  {
    icon: Bug,
    title: 'Find Bugs',
    prompt:
      'Review the codebase for potential bugs, security issues, and error handling gaps. Prioritize by severity.',
  },
  {
    icon: FlaskConical,
    title: 'Write Tests',
    prompt:
      'Generate comprehensive tests for the most critical functions. Focus on edge cases, error handling, and boundary conditions.',
  },
  {
    icon: Wand2,
    title: 'Add Feature',
    prompt: 'Help me plan and implement a new feature. I want to add ',
  },
]

export const MessageListLoading: Component = () => (
  <div class="space-y-4 animate-pulse">
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
    <div class="h-24 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-3/4 ml-auto" />
    <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
  </div>
)

const WorkflowCards: Component = () => {
  const { workflows, applyWorkflow } = useWorkflows()
  const topWorkflows = () => workflows().slice(0, 4)

  return (
    <Show when={topWorkflows().length > 0}>
      <div class="mt-4 max-w-md w-full">
        <div class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Workflows
        </div>
        <div class="grid grid-cols-2 gap-2">
          <For each={topWorkflows()}>
            {(workflow) => (
              <button
                type="button"
                onClick={() => applyWorkflow(workflow)}
                class="
                  flex items-start gap-2.5 p-3 text-left
                  rounded-[var(--radius-lg)]
                  border border-[var(--border-subtle)]
                  bg-[var(--surface-raised)]
                  hover:border-[var(--accent-muted)] hover:bg-[var(--alpha-white-3)]
                  transition-colors
                  group
                "
              >
                <Play class="w-4 h-4 mt-0.5 flex-shrink-0 text-[var(--text-muted)] group-hover:text-[var(--accent)]" />
                <div class="min-w-0 flex-1">
                  <div class="text-xs text-[var(--text-secondary)] group-hover:text-[var(--text-primary)] truncate">
                    {workflow.name}
                  </div>
                  <Show when={workflow.description}>
                    <div class="text-[10px] text-[var(--text-muted)] truncate">
                      {workflow.description}
                    </div>
                  </Show>
                </div>
                <Show when={workflow.usageCount > 0}>
                  <span class="text-[9px] text-[var(--text-muted)] tabular-nums shrink-0">
                    {workflow.usageCount}x
                  </span>
                </Show>
              </button>
            )}
          </For>
        </div>
      </div>
    </Show>
  )
}

export const MessageListEmpty: Component = () => (
  <div class="flex flex-col items-center justify-center h-full">
    <div
      class="
        w-16 h-16 mb-6
        rounded-[var(--radius-xl)]
        bg-[var(--accent-subtle)]
        flex items-center justify-center
      "
    >
      <Sparkles class="w-8 h-8 text-[var(--accent)]" />
    </div>
    <h2 class="text-xl font-semibold text-[var(--text-primary)] font-display">Welcome to AVA</h2>
    <p class="text-sm text-[var(--text-tertiary)] mt-2 max-w-sm text-center">
      Your AI coding assistant is ready. Start a conversation or try a template.
    </p>

    {/* Starter templates */}
    <div class="mt-6 grid grid-cols-2 gap-2 max-w-md w-full">
      <For each={STARTER_TEMPLATES}>
        {(template) => (
          <button
            type="button"
            onClick={() => {
              window.dispatchEvent(
                new CustomEvent('ava:set-input', { detail: { text: template.prompt } })
              )
            }}
            class="
              flex items-start gap-2.5 p-3 text-left
              rounded-[var(--radius-lg)]
              border border-[var(--border-subtle)]
              bg-[var(--surface-raised)]
              hover:border-[var(--accent-muted)] hover:bg-[var(--alpha-white-3)]
              transition-colors
              group
            "
          >
            <template.icon class="w-4 h-4 mt-0.5 flex-shrink-0 text-[var(--text-muted)] group-hover:text-[var(--accent)]" />
            <span class="text-xs text-[var(--text-secondary)] group-hover:text-[var(--text-primary)]">
              {template.title}
            </span>
          </button>
        )}
      </For>
    </div>

    {/* Workflow cards */}
    <WorkflowCards />
  </div>
)

interface ScrollToBottomButtonProps {
  onClick: () => void
}

export const ScrollToBottomButton: Component<ScrollToBottomButtonProps> = (props) => (
  <button
    type="button"
    onClick={props.onClick}
    class="
      absolute bottom-4 right-8
      p-2 rounded-full
      bg-[var(--surface-raised)] border border-[var(--border-subtle)]
      shadow-md
      text-[var(--text-secondary)]
      hover:bg-[var(--accent)] hover:text-white hover:border-[var(--accent)]
      transition-all duration-[var(--duration-fast)]
      z-10
    "
    title="Scroll to bottom"
  >
    <svg
      class="w-5 h-5"
      fill="none"
      viewBox="0 0 24 24"
      stroke="currentColor"
      aria-labelledby="scroll-icon-title"
    >
      <title id="scroll-icon-title">Scroll to bottom</title>
      <path
        stroke-linecap="round"
        stroke-linejoin="round"
        stroke-width="2"
        d="M19 14l-7 7m0 0l-7-7m7 7V3"
      />
    </svg>
  </button>
)
