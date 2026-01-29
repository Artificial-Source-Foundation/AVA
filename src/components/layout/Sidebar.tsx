import { Component } from "solid-js";

export const Sidebar: Component = () => {
  return (
    <aside class="w-64 border-r border-gray-700 bg-gray-800 flex flex-col">
      {/* Logo/Title */}
      <div class="p-4 border-b border-gray-700">
        <h1 class="text-xl font-bold text-white">Estela</h1>
        <p class="text-xs text-gray-400 mt-1">Multi-Agent AI Assistant</p>
      </div>

      {/* Navigation */}
      <nav class="flex-1 p-4 space-y-2">
        <button class="w-full text-left px-3 py-2 rounded-lg bg-gray-700 text-white hover:bg-gray-600 transition">
          + New Session
        </button>

        {/* Session list placeholder */}
        <div class="mt-4">
          <h2 class="text-xs font-semibold text-gray-500 uppercase tracking-wider mb-2">
            Recent Sessions
          </h2>
          <div class="space-y-1">
            <p class="text-sm text-gray-500 italic px-3">No sessions yet</p>
          </div>
        </div>
      </nav>

      {/* Settings */}
      <div class="p-4 border-t border-gray-700">
        <button class="w-full text-left px-3 py-2 rounded-lg text-gray-400 hover:bg-gray-700 hover:text-white transition">
          Settings
        </button>
      </div>
    </aside>
  );
};
