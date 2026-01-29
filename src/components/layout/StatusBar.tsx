import { Component } from "solid-js";

export const StatusBar: Component = () => {
  return (
    <div class="flex items-center justify-between h-6 bg-gray-800 border-t border-gray-700 px-4 text-xs text-gray-500">
      {/* Left side - Agent status */}
      <div class="flex items-center space-x-4">
        <span class="flex items-center">
          <span class="w-2 h-2 rounded-full bg-green-500 mr-2"></span>
          Ready
        </span>
      </div>

      {/* Right side - Info */}
      <div class="flex items-center space-x-4">
        <span>Estela v0.1.0</span>
      </div>
    </div>
  );
};
