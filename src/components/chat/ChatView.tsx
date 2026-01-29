import { Component } from "solid-js";
import { MessageList } from "./MessageList";
import { MessageInput } from "./MessageInput";

export const ChatView: Component = () => {
  return (
    <div class="flex flex-col h-full">
      {/* Messages area */}
      <MessageList />

      {/* Input area */}
      <MessageInput />
    </div>
  );
};
