import { type Component, type JSX, Show } from 'solid-js'

interface ChatSurfaceProps {
  header?: JSX.Element
  messages: JSX.Element
  docks?: JSX.Element
  status?: JSX.Element
  input: JSX.Element
  overlays?: JSX.Element
  class?: string
}

export const ChatSurface: Component<ChatSurfaceProps> = (props) => {
  const classes = () =>
    ['flex h-full min-h-0 flex-col bg-[var(--background)]', props.class].filter(Boolean).join(' ')

  return (
    <div class={classes()}>
      <Show when={props.header}>{props.header}</Show>
      {props.messages}
      <Show when={props.docks}>{props.docks}</Show>
      <Show when={props.status}>{props.status}</Show>
      {props.input}
      <Show when={props.overlays}>{props.overlays}</Show>
    </div>
  )
}
