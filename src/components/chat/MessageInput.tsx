import { Component, createSignal } from "solid-js";

export const MessageInput: Component = () => {
  const [input, setInput] = createSignal("");

  const handleSubmit = (e: Event) => {
    e.preventDefault();
    const message = input().trim();
    if (!message) return;

    // TODO: Send message to agent system
    console.log("Send message:", message);
    setInput("");
  };

  return (
    <form onSubmit={handleSubmit} class="p-4 border-t border-gray-700">
      <div class="flex space-x-4">
        <input
          type="text"
          value={input()}
          onInput={(e) => setInput(e.currentTarget.value)}
          placeholder="Type a message..."
          class="flex-1 bg-gray-700 text-white placeholder-gray-400 rounded-lg px-4 py-3 focus:outline-none focus:ring-2 focus:ring-blue-500"
        />
        <button
          type="submit"
          class="px-6 py-3 bg-blue-600 text-white rounded-lg hover:bg-blue-500 focus:outline-none focus:ring-2 focus:ring-blue-500 transition"
        >
          Send
        </button>
      </div>
    </form>
  );
};
