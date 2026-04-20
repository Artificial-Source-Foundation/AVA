/**
 * Attachment Utilities
 *
 * Pure helper functions for processing images, text files, and pastes.
 * No SolidJS signals — every function is side-effect-free.
 */

import {
  ACCEPTED_IMAGE_TYPES,
  MAX_FILE_SIZE,
  MAX_IMAGE_SIZE,
  PASTE_PREVIEW_LINES,
  type PendingFile,
  type PendingImage,
  type SupportedImageMimeType,
  TEXT_EXTENSIONS,
} from './types'

function isSupportedImageMimeType(type: string): type is SupportedImageMimeType {
  return ACCEPTED_IMAGE_TYPES.includes(type as SupportedImageMimeType)
}

// ---------------------------------------------------------------------------
// Image processing
// ---------------------------------------------------------------------------

/** Read a File as a base64 image payload. Returns null for unsupported/oversized files. */
export function processImageFile(file: File): Promise<PendingImage | null> {
  if (!isSupportedImageMimeType(file.type)) return Promise.resolve(null)
  if (file.size > MAX_IMAGE_SIZE) return Promise.resolve(null)
  const mimeType = file.type
  return new Promise((resolve) => {
    const reader = new FileReader()
    reader.onload = () => {
      const result = reader.result as string
      const base64 = result.split(',')[1]
      if (base64) {
        resolve({ data: base64, mimeType, name: file.name })
      } else {
        resolve(null)
      }
    }
    reader.onerror = () => resolve(null)
    reader.readAsDataURL(file)
  })
}

// ---------------------------------------------------------------------------
// Text-file processing
// ---------------------------------------------------------------------------

/** Read a text file and return its content. Returns null for unsupported/oversized files. */
export function processTextFile(file: File): Promise<PendingFile | null> {
  const ext = `.${file.name.split('.').pop()?.toLowerCase()}`
  if (!TEXT_EXTENSIONS.has(ext)) return Promise.resolve(null)
  if (file.size > MAX_FILE_SIZE) return Promise.resolve(null)
  return new Promise((resolve) => {
    const reader = new FileReader()
    reader.onload = () => resolve({ name: file.name, content: reader.result as string })
    reader.onerror = () => resolve(null)
    reader.readAsText(file)
  })
}

// ---------------------------------------------------------------------------
// Paste helpers
// ---------------------------------------------------------------------------

/** Return the first few lines of a pasted block for collapsed preview. */
export function getPastePreview(content: string): string {
  return content.split('\n').slice(0, PASTE_PREVIEW_LINES).join('\n')
}

// ---------------------------------------------------------------------------
// Message composition
// ---------------------------------------------------------------------------

/** Build the full message by prepending file blocks and appending paste blocks. */
export function buildFullMessage(
  message: string,
  files: PendingFile[],
  pastes: { content: string }[]
): string {
  let fullMessage = message

  if (files.length > 0) {
    const fileBlocks = files
      .map((f) => {
        const ext = f.name.split('.').pop() || ''
        return `**${f.name}:**\n\`\`\`${ext}\n${f.content}\n\`\`\``
      })
      .join('\n\n')
    fullMessage = `${fileBlocks}\n\n${fullMessage}`
  }

  if (pastes.length > 0) {
    const pasteBlocks = pastes.map((p) => `\`\`\`\n${p.content}\n\`\`\``).join('\n\n')
    fullMessage = `${fullMessage}\n\n${pasteBlocks}`
  }

  return fullMessage
}
