import { createSignal } from "solid-js";
import type { Session, Message, Agent } from "../types";

// Current session state
const [currentSession, setCurrentSession] = createSignal<Session | null>(null);
const [messages, setMessages] = createSignal<Message[]>([]);
const [agents, setAgents] = createSignal<Agent[]>([]);

// Session actions
export function useSession() {
  return {
    currentSession,
    setCurrentSession,
    messages,
    setMessages,
    agents,
    setAgents,

    // Add a message to the current session
    addMessage: (message: Message) => {
      setMessages((prev) => [...prev, message]);
    },

    // Update an agent's status
    updateAgent: (id: string, updates: Partial<Agent>) => {
      setAgents((prev) =>
        prev.map((agent) =>
          agent.id === id ? { ...agent, ...updates } : agent
        )
      );
    },

    // Clear session state
    clearSession: () => {
      setCurrentSession(null);
      setMessages([]);
      setAgents([]);
    },
  };
}
