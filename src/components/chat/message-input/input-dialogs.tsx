/**
 * Input Dialogs
 *
 * Modal dialogs rendered alongside the MessageInput:
 * - ModelBrowserDialog (model selection)
 * - ExpandedEditor (Ctrl+E full-screen editor)
 * - SandboxReviewDialog (sandbox change review)
 */

import { type Accessor, type Component, onMount, Show, untrack } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { useLayout } from '../../../stores/layout'
import { useSandbox } from '../../../stores/sandbox'
import { useSession } from '../../../stores/session'
import { ModelBrowserDialog } from '../../dialogs/model-browser/model-browser-dialog'
import { SandboxReviewDialog } from '../../dialogs/SandboxReviewDialog'
import { ExpandedEditor } from '../ExpandedEditor'

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface InputDialogsProps {
  input: Accessor<string>
  setInput: (v: string) => void
  autoResize: () => void
  enabledProviders: Accessor<LLMProviderConfig[]>
  focusTextarea: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const InputDialogs: Component<InputDialogsProps> = (props) => {
  const sessionStore = useSession()
  const {
    modelBrowserOpen,
    modelBrowserRequest,
    closeModelBrowser,
    expandedEditorOpen,
    setExpandedEditorOpen,
  } = useLayout()
  const sandbox = useSandbox()
  let focusTextarea = () => {}
  let autoResize = () => {}

  onMount(() => {
    focusTextarea = untrack(() => props.focusTextarea)
    autoResize = untrack(() => props.autoResize)
  })

  const handleApplySelected = (paths: string[]): Promise<void> =>
    sandbox.applySelectedChanges(paths).then(() => {
      if (untrack(() => sandbox.pendingCount()) === 0) sandbox.closeReview()
    })

  const handleApplyAll = (): Promise<void> =>
    sandbox.applyAllChanges().then(() => {
      sandbox.closeReview()
    })

  return (
    <>
      <Show when={modelBrowserOpen()}>
        <ModelBrowserDialog
          open={modelBrowserOpen}
          onOpenChange={(open) => {
            if (!open) closeModelBrowser()
          }}
          selectedModel={modelBrowserRequest()?.selectedModel ?? sessionStore.selectedModel}
          selectedProvider={
            modelBrowserRequest()?.selectedProvider ?? sessionStore.selectedProvider
          }
          onSelect={(modelId, providerId) => {
            const request = modelBrowserRequest()
            if (request) {
              request.onSelect(modelId, providerId)
              return
            }
            sessionStore.setSelectedModel(modelId, providerId)
          }}
          enabledProviders={modelBrowserRequest()?.enabledProviders ?? props.enabledProviders}
        />
      </Show>
      <Show when={expandedEditorOpen()}>
        <ExpandedEditor
          open={true}
          initialText={props.input()}
          onApply={(text) => {
            props.setInput(text)
            setExpandedEditorOpen(false)
            queueMicrotask(() => {
              focusTextarea()
              autoResize()
            })
          }}
          onClose={() => setExpandedEditorOpen(false)}
        />
      </Show>
      <SandboxReviewDialog
        open={sandbox.reviewDialogOpen()}
        changes={sandbox.pendingChanges()}
        onApplySelected={handleApplySelected}
        onApplyAll={handleApplyAll}
        onRejectAll={() => {
          sandbox.rejectAllChanges()
          sandbox.closeReview()
        }}
        onClose={() => sandbox.closeReview()}
      />
    </>
  )
}
