/**
 * Splash screen shown during app initialization.
 * Minimal design: black background, centered logo mark with blue glow,
 * barely-visible "ava" text near the bottom.
 */

import { createSignal, Show } from 'solid-js'

interface SplashScreenProps {
  /** When false, triggers the fade-out animation then unmounts */
  visible: boolean
  /** Current initialization step (kept for API compat, not displayed) */
  status?: string
  /** Optional explicit progress 0-100 (kept for API compat, not displayed) */
  progress?: number
}

export function SplashScreen(props: SplashScreenProps): ReturnType<typeof Show> {
  const [shouldRender, setShouldRender] = createSignal(true)

  const onTransitionEnd = (): void => {
    if (!props.visible) {
      setShouldRender(false)
    }
  }

  return (
    <Show when={shouldRender()}>
      <div
        class="fixed inset-0 z-[9999] flex items-center justify-center"
        classList={{ 'splash-fade-out': !props.visible }}
        onTransitionEnd={onTransitionEnd}
        style={{ background: 'var(--background)' }}
        aria-live="polite"
      >
        {/* Logo mark — 64x64 rounded square with blue-purple gradient and blue glow */}
        <div
          class="splash-logo flex h-16 w-16 items-center justify-center rounded-[16px]"
          style={{
            background:
              'linear-gradient(180deg, var(--accent) 0%, color-mix(in srgb, var(--accent) 60%, var(--system-purple)) 100%)',
            'box-shadow': '0 0 80px color-mix(in srgb, var(--accent) 10%, transparent)',
          }}
        >
          <span class="select-none text-[28px] font-extrabold leading-none text-[var(--text-on-accent)]">
            A
          </span>
        </div>

        {/* "ava" text — barely visible, near bottom */}
        <span class="pointer-events-none absolute bottom-[30px] left-1/2 -translate-x-1/2 select-none font-ui-mono text-[11px] tracking-[4px] text-[var(--surface-raised)]">
          ava
        </span>
      </div>
    </Show>
  )
}
