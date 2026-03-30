import { type Component, type JSX, Show } from 'solid-js'

interface ChatComposerProps {
  doomBanner?: JSX.Element
  popovers?: JSX.Element
  textarea: JSX.Element
  shortcutHint?: JSX.Element
  toolbar?: JSX.Element
  dialogs?: JSX.Element
  class?: string
}

export const ChatComposer: Component<ChatComposerProps> = (props) => {
  return (
    <div class={['flex justify-center px-5 pb-5 pt-0', props.class].filter(Boolean).join(' ')}>
      <div class="w-full max-w-[min(94%,1400px)]">
        <Show when={props.doomBanner}>{props.doomBanner}</Show>
        <Show when={props.popovers}>{props.popovers}</Show>

        {/* Floating composer card */}
        <div
          class="rounded-[14px] border border-[var(--border-default)] bg-[var(--surface)]"
          style={{
            'box-shadow': '0 4px 24px 0 var(--alpha-black-40), 0 1px 4px 0 var(--alpha-black-20)',
          }}
        >
          {/* Input row */}
          <div class="px-4 py-4">{props.textarea}</div>

          {/* Toolbar */}
          <Show when={props.toolbar}>
            <div class="px-3.5 pb-3 pt-0">{props.toolbar}</div>
          </Show>
        </div>

        <Show when={props.shortcutHint}>
          <div class="mt-1.5">{props.shortcutHint}</div>
        </Show>
        <Show when={props.dialogs}>{props.dialogs}</Show>
      </div>
    </div>
  )
}
