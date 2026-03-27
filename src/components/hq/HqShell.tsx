import { type Component, createSignal, Show } from 'solid-js'
import { useHq } from '../../stores/hq'
import { HqContent } from './HqContent'
import { HqNewEpicModal } from './HqNewEpicModal'
import { HqOnboarding } from './HqOnboarding'
import { HqSidebar } from './HqSidebar'

export const HqShell: Component = () => {
  const { isOnboarded, createEpic, showNewEpicModal, closeNewEpicModal } = useHq()
  const [showOnboarding, setShowOnboarding] = createSignal(!isOnboarded())

  return (
    <div class="relative flex h-full w-full" style={{ 'background-color': 'var(--background)' }}>
      <HqSidebar />
      <HqContent />

      {/* Onboarding overlay — shown once */}
      <Show when={showOnboarding()}>
        <HqOnboarding onComplete={() => setShowOnboarding(false)} />
      </Show>

      {/* New Epic modal */}
      <Show when={showNewEpicModal()}>
        <HqNewEpicModal
          onClose={closeNewEpicModal}
          onCreate={(title, description) => void createEpic(title, description)}
        />
      </Show>
    </div>
  )
}
