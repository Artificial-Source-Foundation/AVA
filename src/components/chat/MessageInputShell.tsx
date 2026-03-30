import type { Component, JSX } from 'solid-js'
import { ChatComposer } from './ChatComposer'
import { ShortcutHint } from './ShortcutHint'

interface MessageInputShellProps {
  doomBanner?: JSX.Element
  popovers?: JSX.Element
  textarea: JSX.Element
  toolbar: JSX.Element
  dialogs?: JSX.Element
  shortcutHintSendCount: number
}

export const MessageInputShell: Component<MessageInputShellProps> = (props) => (
  <ChatComposer
    doomBanner={props.doomBanner}
    popovers={props.popovers}
    textarea={props.textarea}
    shortcutHint={<ShortcutHint sendCount={props.shortcutHintSendCount} />}
    toolbar={props.toolbar}
    dialogs={props.dialogs}
  />
)
