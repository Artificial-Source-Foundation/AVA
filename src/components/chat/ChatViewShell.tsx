import type { Component, JSX } from 'solid-js'
import { ChatSurface } from './ChatSurface'

interface ChatViewShellProps {
  header?: JSX.Element
  messages: JSX.Element
  docks?: JSX.Element
  status?: JSX.Element
  input: JSX.Element
  overlays?: JSX.Element
  class?: string
}

export const ChatViewShell: Component<ChatViewShellProps> = (props) => (
  <ChatSurface
    header={props.header}
    messages={props.messages}
    docks={props.docks}
    status={props.status}
    input={props.input}
    overlays={props.overlays}
    class={props.class}
  />
)
