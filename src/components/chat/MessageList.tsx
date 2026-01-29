import { Component, For } from "solid-js";
import { useSession } from "../../stores/session";

export const MessageList: Component = () => {
  const { messages } = useSession();

  return (
    <div class="flex-1 overflow-y-auto p-4 space-y-4">
      <For each={messages()} fallback={
        <div class="flex items-center justify-center h-full text-gray-500">
          <div class="text-center">
            <p class="text-lg">Welcome to Estela</p>
            <p class="text-sm mt-2">Start a conversation to begin</p>
          </div>
        </div>
      }>
        {(message) => (
          <div class={`flex ${message.role === 'user' ? 'justify-end' : 'justify-start'}`}>
            <div class={`max-w-[80%] rounded-lg px-4 py-2 ${
              message.role === 'user'
                ? 'bg-blue-600 text-white'
                : 'bg-gray-700 text-gray-100'
            }`}>
              <p class="whitespace-pre-wrap">{message.content}</p>
            </div>
          </div>
        )}
      </For>
    </div>
  );
};
