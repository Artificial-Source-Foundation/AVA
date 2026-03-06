/**
 * Input Dialogs
 *
 * Modal dialogs rendered alongside the MessageInput:
 * - ModelBrowserDialog (model selection)
 * - ExpandedEditor (Ctrl+E full-screen editor)
 * - SandboxReviewDialog (sandbox change review)
 */

import type { Accessor, Component } from 'solid-js'
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
  const { modelBrowserOpen, closeModelBrowser, expandedEditorOpen, setExpandedEditorOpen } =
    useLayout()
  const sandbox = useSandbox()

  return (
    <>
      <ModelBrowserDialog
        open={modelBrowserOpen}
        onOpenChange={(open) => {
          if (!open) closeModelBrowser()
        }}
        selectedModel={sessionStore.selectedModel}
        selectedProvider={sessionStore.selectedProvider}
        onSelect={(modelId, providerId) => sessionStore.setSelectedModel(modelId, providerId)}
        enabledProviders={props.enabledProviders}
      />
      <ExpandedEditor
        open={expandedEditorOpen()}
        initialText={props.input()}
        onApply={(text) => {
          props.setInput(text)
          setExpandedEditorOpen(false)
          queueMicrotask(() => {
            props.focusTextarea()
            props.autoResize()
          })
        }}
        onClose={() => setExpandedEditorOpen(false)}
      />
      <SandboxReviewDialog
        open={sandbox.reviewDialogOpen()}
        changes={sandbox.pendingChanges()}
        onApplySelected={async (paths) => {
          await sandbox.applySelectedChanges(paths)
          if (sandbox.pendingCount() === 0) sandbox.closeReview()
        }}
        onApplyAll={async () => {
          await sandbox.applyAllChanges()
          sandbox.closeReview()
        }}
        onRejectAll={() => {
          sandbox.rejectAllChanges()
          sandbox.closeReview()
        }}
        onClose={() => sandbox.closeReview()}
      />
    </>
  )
}
