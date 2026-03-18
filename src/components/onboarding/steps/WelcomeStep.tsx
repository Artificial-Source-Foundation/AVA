/**
 * Step 1: Welcome
 *
 * Full-screen centered welcome with AVA logo, tagline, and get-started CTA.
 */

import type { Component } from 'solid-js'

export interface WelcomeStepProps {
  onNext: () => void
  onImport?: () => void
}

export const WelcomeStep: Component<WelcomeStepProps> = (props) => (
  <div class="flex flex-col items-center text-center">
    {/* AVA Logo */}
    <div
      class="w-20 h-20 rounded-[20px] flex items-center justify-center mb-8"
      style={{
        background: 'linear-gradient(135deg, rgba(139, 92, 246, 0.25), rgba(139, 92, 246, 0.10))',
      }}
    >
      <span class="text-[#A78BFA] text-4xl font-bold select-none">A</span>
    </div>

    {/* Title */}
    <h1 class="text-[32px] font-bold text-[#FAFAFA] leading-tight tracking-tight mb-3">
      Welcome to AVA
    </h1>

    {/* Subtitle */}
    <p class="text-base text-[#71717A] mb-10 max-w-sm">
      Your AI dev team — lean by default, infinitely extensible
    </p>

    {/* Get Started button */}
    <button
      type="button"
      onClick={props.onNext}
      class="px-10 py-3 bg-[#A78BFA] hover:bg-[#8B5CF6] text-white text-sm font-semibold rounded-xl transition-colors"
    >
      Get Started
    </button>

    {/* Import link */}
    <button
      type="button"
      onClick={() => props.onImport?.()}
      class="mt-6 text-sm text-[#52525B] hover:text-[#71717A] transition-colors"
    >
      Already have config? Import .ava/ folder &rarr;
    </button>
  </div>
)
