import type { Component } from 'solid-js'

export const TabBar: Component = () => {
  return (
    <div class="flex items-center h-10 bg-gray-800 border-b border-gray-700 px-2">
      {/* Active tab */}
      <div class="flex items-center px-4 py-1 bg-gray-900 rounded-t-lg text-sm text-white">
        <span>New Session</span>
        <button type="button" class="ml-2 text-gray-500 hover:text-white">
          ×
        </button>
      </div>

      {/* Add tab button */}
      <button
        type="button"
        class="ml-2 px-2 py-1 text-gray-500 hover:text-white hover:bg-gray-700 rounded"
      >
        +
      </button>
    </div>
  )
}
