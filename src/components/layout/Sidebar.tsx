/**
 * Sidebar Component
 * Main navigation sidebar with session list and settings
 */

import { type Component, createSignal } from 'solid-js'
import { SessionList } from '../sessions'
import { SettingsModal } from '../settings'

export const Sidebar: Component = () => {
  const [showSettings, setShowSettings] = createSignal(false)

  return (
    <>
      <aside class="w-64 border-r border-gray-700 bg-gray-800 flex flex-col">
        {/* Logo/Title */}
        <div class="p-4 border-b border-gray-700">
          <h1 class="text-xl font-bold text-white">Estela</h1>
          <p class="text-xs text-gray-400 mt-1">Multi-Agent AI Assistant</p>
        </div>

        {/* Session list */}
        <div class="flex-1 p-3 overflow-hidden">
          <SessionList />
        </div>

        {/* Settings button */}
        <div class="p-3 border-t border-gray-700">
          <button
            type="button"
            onClick={() => setShowSettings(true)}
            class="w-full flex items-center gap-3 px-3 py-2.5 rounded-lg text-gray-400 hover:bg-gray-700 hover:text-white transition-colors"
          >
            <svg
              class="w-5 h-5"
              fill="none"
              stroke="currentColor"
              viewBox="0 0 24 24"
              role="img"
              aria-label="Settings"
            >
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z"
              />
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M15 12a3 3 0 11-6 0 3 3 0 016 0z"
              />
            </svg>
            <span class="font-medium">Settings</span>
          </button>
        </div>
      </aside>

      {/* Settings Modal */}
      <SettingsModal isOpen={showSettings()} onClose={() => setShowSettings(false)} />
    </>
  )
}
