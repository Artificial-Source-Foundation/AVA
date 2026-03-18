/**
 * Splash screen shown during app initialization.
 * Displays AVA logo (rounded with purple accent), app name, tagline,
 * slim progress bar, status text, and version at bottom.
 * Fades out when `visible` becomes false, then unmounts.
 */

import { createMemo, createSignal, Show } from 'solid-js'

/** Known init steps in approximate order — used to estimate progress */
const INIT_STEPS = [
  'Starting logger',
  'Initializing platform',
  'Loading settings',
  'Checking backend',
  'Loading models',
  'Loading providers',
  'Initializing core',
  'Loading database',
  'Loading projects',
  'Loading plugins',
  'Restoring',
  'Ready',
] as const

function estimateProgress(status: string | undefined): number {
  if (!status) return 0
  const lower = status.toLowerCase()
  for (let i = INIT_STEPS.length - 1; i >= 0; i--) {
    if (lower.includes(INIT_STEPS[i].toLowerCase())) {
      // Map to 5%–95% range so it never looks "done" until we fade out
      return Math.round(5 + ((i + 1) / INIT_STEPS.length) * 90)
    }
  }
  return 5
}

interface SplashScreenProps {
  /** When false, triggers the fade-out animation then unmounts */
  visible: boolean
  /** Current initialization step, e.g. "Loading database..." */
  status?: string
  /** Optional explicit progress 0-100 */
  progress?: number
}

export function SplashScreen(props: SplashScreenProps) {
  const [shouldRender, setShouldRender] = createSignal(true)

  const onTransitionEnd = (): void => {
    if (!props.visible) {
      setShouldRender(false)
    }
  }

  const progressPct = createMemo(() => {
    if (props.progress != null) return props.progress
    return estimateProgress(props.status)
  })

  return (
    <Show when={shouldRender()}>
      <div
        class="fixed inset-0 z-[9999] flex flex-col items-center justify-center bg-[var(--background)]"
        classList={{ 'splash-fade-out': !props.visible }}
        onTransitionEnd={onTransitionEnd}
      >
        {/* Mesh gradient background */}
        <div class="splash-mesh" />

        {/* Logo — 72x72 rounded with purple accent background */}
        <div class="splash-logo mb-5">
          <div
            class="flex items-center justify-center rounded-[18px]"
            style={{
              width: '72px',
              height: '72px',
              background: '#A78BFA15',
            }}
          >
            <span
              class="font-bold select-none"
              style={{
                'font-size': '32px',
                color: '#A78BFA',
                'line-height': '1',
              }}
            >
              A
            </span>
          </div>
        </div>

        {/* App name */}
        <h1
          class="font-bold tracking-[0.15em] text-[var(--text-primary)] mb-1"
          style={{ 'font-size': '20px' }}
        >
          AVA
        </h1>

        {/* Tagline */}
        <p class="text-xs mb-8" style={{ color: '#52525B' }}>
          Your AI Dev Team
        </p>

        {/* Slim progress bar */}
        <div
          class="rounded-full overflow-hidden mb-4"
          style={{
            width: '200px',
            height: '3px',
            background: '#18181B',
          }}
        >
          <div
            class="h-full rounded-full transition-[width] duration-500 ease-out"
            style={{
              width: `${progressPct()}%`,
              background: '#A78BFA',
            }}
          />
        </div>

        {/* Status text */}
        <p class="text-xs h-4 transition-opacity duration-200" style={{ color: '#3F3F46' }}>
          {props.status ?? ''}
        </p>

        {/* Version — pinned to bottom */}
        <span class="absolute bottom-5 text-[10px] tracking-wide" style={{ color: '#27272A' }}>
          v2.1.0
        </span>
      </div>
    </Show>
  )
}
