/**
 * Splash screen shown during app initialization.
 * Displays logo, app name, tagline, loading status, and version.
 * Fades out when `visible` becomes false, then unmounts.
 */

import { createSignal, Show } from 'solid-js'

interface SplashScreenProps {
  /** When false, triggers the fade-out animation then unmounts */
  visible: boolean
  /** Current initialization step, e.g. "Loading database..." */
  status?: string
}

export function SplashScreen(props: SplashScreenProps) {
  const [shouldRender, setShouldRender] = createSignal(true)

  const onTransitionEnd = () => {
    if (!props.visible) {
      setShouldRender(false)
    }
  }

  return (
    <Show when={shouldRender()}>
      <div
        class="fixed inset-0 z-[9999] flex flex-col items-center justify-center bg-[var(--background)]"
        classList={{ 'splash-fade-out': !props.visible }}
        onTransitionEnd={onTransitionEnd}
      >
        {/* Mesh gradient background — CSS only, GPU composited */}
        <div class="splash-mesh" />

        {/* Logo placeholder — swap with real logo later */}
        <div class="splash-logo mb-5">
          <svg
            width="72"
            height="72"
            viewBox="0 0 64 64"
            fill="none"
            xmlns="http://www.w3.org/2000/svg"
            aria-hidden="true"
          >
            <path
              d="M32 4L58 32L32 60L6 32L32 4Z"
              stroke="var(--accent)"
              stroke-width="2.5"
              fill="none"
              opacity="0.3"
            />
            <path
              d="M32 12L50 32L32 52L14 32L32 12Z"
              stroke="var(--accent)"
              stroke-width="2"
              fill="var(--accent)"
              opacity="0.15"
            />
            <path d="M32 20L42 32L32 44L22 32L32 20Z" fill="var(--accent)" opacity="0.6" />
          </svg>
        </div>

        {/* App name */}
        <h1 class="text-2xl font-semibold tracking-[0.25em] uppercase text-[var(--text-primary)] mb-1.5">
          Estela
        </h1>

        {/* Tagline */}
        <p class="text-xs text-[var(--text-muted)] tracking-wide mb-10">AI Coding Companion</p>

        {/* Loading dots */}
        <div class="flex gap-1.5 mb-4">
          <span
            class="splash-dot w-1.5 h-1.5 rounded-full bg-[var(--accent)]"
            style="animation-delay: 0ms"
          />
          <span
            class="splash-dot w-1.5 h-1.5 rounded-full bg-[var(--accent)]"
            style="animation-delay: 150ms"
          />
          <span
            class="splash-dot w-1.5 h-1.5 rounded-full bg-[var(--accent)]"
            style="animation-delay: 300ms"
          />
        </div>

        {/* Status text */}
        <p class="text-xs text-[var(--text-muted)] h-4 transition-opacity duration-200">
          {props.status ?? ''}
        </p>

        {/* Version — pinned to bottom */}
        <span class="absolute bottom-5 text-[10px] text-[var(--text-muted)] opacity-40 tracking-wide">
          v0.1.0
        </span>
      </div>
    </Show>
  )
}
