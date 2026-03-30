import { type Component, type JSX, Show } from 'solid-js'

interface ChatHeaderBarProps {
  title: JSX.Element
  leftMeta?: JSX.Element
  right?: JSX.Element
  class?: string
}

export const ChatHeaderBar: Component<ChatHeaderBarProps> = (props) => {
  return (
    <div
      class={[
        'flex items-center justify-between h-[52px] min-h-[52px] px-5 border-b border-[var(--border-subtle)] select-none',
        props.class,
      ]
        .filter(Boolean)
        .join(' ')}
    >
      <div class="flex items-center gap-2.5 min-w-0">
        {props.title}
        <Show when={props.leftMeta}>{props.leftMeta}</Show>
      </div>
      <Show when={props.right}>{props.right}</Show>
    </div>
  )
}
