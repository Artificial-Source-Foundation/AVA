/**
 * API Keys Onboarding Step
 *
 * Collects Anthropic and OpenRouter API keys during onboarding.
 * Extracted from OnboardingDialog.tsx to keep each module under 300 lines.
 */

import { Eye, EyeOff, Shield } from 'lucide-solid'
import { type Accessor, type Component, type Setter, Show } from 'solid-js'
import { NavButtons } from './OnboardingSteps'

// ============================================================================
// API Keys Step
// ============================================================================

export const ApiKeysStep: Component<{
  anthropicKey: Accessor<string>
  setAnthropicKey: Setter<string>
  showAnthropicKey: Accessor<boolean>
  setShowAnthropicKey: Setter<boolean>
  openrouterKey: Accessor<string>
  setOpenrouterKey: Setter<string>
  showOpenrouterKey: Accessor<boolean>
  setShowOpenrouterKey: Setter<boolean>
  canGoNext: Accessor<boolean>
  onPrev: () => void
  onNext: () => void
}> = (props) => (
  <div class="step-enter flex flex-col">
    <div class="stagger-child text-center mb-6">
      <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-1">
        Connect Your API
      </h2>
      <p class="text-sm text-[var(--text-muted)]">Add at least one key to get started</p>
    </div>

    {/* Anthropic */}
    <div class="stagger-child mb-4">
      <span class="block text-sm font-medium text-[var(--text-secondary)] mb-1.5">
        Anthropic API Key
      </span>
      <div class="relative">
        <input
          type={props.showAnthropicKey() ? 'text' : 'password'}
          value={props.anthropicKey()}
          onInput={(e) => props.setAnthropicKey(e.currentTarget.value)}
          placeholder="sk-ant-api03-..."
          class="onboarding-input w-full px-4 py-2.5 pr-10 bg-[var(--surface-glass)] text-[var(--text-primary)] placeholder-[var(--text-muted)] border border-[var(--border-default)] rounded-xl text-sm transition-all outline-none"
        />
        <button
          type="button"
          onClick={() => props.setShowAnthropicKey(!props.showAnthropicKey())}
          class="absolute right-3 top-1/2 -translate-y-1/2 text-[var(--text-tertiary)] hover:text-[var(--text-primary)] transition-colors"
        >
          <Show when={props.showAnthropicKey()} fallback={<Eye class="w-4 h-4" />}>
            <EyeOff class="w-4 h-4" />
          </Show>
        </button>
      </div>
      <p class="text-xs text-[var(--text-muted)] mt-1">Direct access to Claude models</p>
    </div>

    {/* OpenRouter */}
    <div class="stagger-child mb-4">
      <span class="block text-sm font-medium text-[var(--text-secondary)] mb-1.5">
        OpenRouter API Key
      </span>
      <div class="relative">
        <input
          type={props.showOpenrouterKey() ? 'text' : 'password'}
          value={props.openrouterKey()}
          onInput={(e) => props.setOpenrouterKey(e.currentTarget.value)}
          placeholder="sk-or-v1-..."
          class="onboarding-input w-full px-4 py-2.5 pr-10 bg-[var(--surface-glass)] text-[var(--text-primary)] placeholder-[var(--text-muted)] border border-[var(--border-default)] rounded-xl text-sm transition-all outline-none"
        />
        <button
          type="button"
          onClick={() => props.setShowOpenrouterKey(!props.showOpenrouterKey())}
          class="absolute right-3 top-1/2 -translate-y-1/2 text-[var(--text-tertiary)] hover:text-[var(--text-primary)] transition-colors"
        >
          <Show when={props.showOpenrouterKey()} fallback={<Eye class="w-4 h-4" />}>
            <EyeOff class="w-4 h-4" />
          </Show>
        </button>
      </div>
      <p class="text-xs text-[var(--text-muted)] mt-1">Access to 100+ models</p>
    </div>

    {/* Security note */}
    <div class="stagger-child flex items-center gap-2.5 px-3 py-2.5 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-xl mb-6">
      <Shield class="w-4 h-4 text-[var(--success)] flex-shrink-0" />
      <p class="text-xs text-[var(--text-secondary)]">
        Keys are stored locally and never sent to any server except the provider.
      </p>
    </div>

    <NavButtons
      onPrev={props.onPrev}
      onNext={props.onNext}
      nextLabel={props.canGoNext() ? 'Continue' : 'Skip for now'}
    />
  </div>
)
