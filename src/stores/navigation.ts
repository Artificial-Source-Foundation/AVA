/**
 * Navigation Store
 * Global state for main app navigation (chat vs settings page)
 * When entering settings, sidebar collapses for focus.
 */

import { createSignal } from 'solid-js'
import { useLayout } from './layout'

export type MainView = 'chat' | 'settings'

const [currentView, setCurrentView] = createSignal<MainView>('chat')

// Remember sidebar state before entering settings
let sidebarWasVisible = true

export function useNavigation() {
  const { sidebarVisible, setSidebarVisible } = useLayout()

  const navigateTo = (view: MainView) => {
    setCurrentView(view)
  }

  const goToChat = () => {
    setCurrentView('chat')
    setSidebarVisible(sidebarWasVisible)
  }

  const goToSettings = () => {
    sidebarWasVisible = sidebarVisible()
    setSidebarVisible(false)
    setCurrentView('settings')
  }

  return {
    currentView,
    navigateTo,
    goToChat,
    goToSettings,
  }
}
