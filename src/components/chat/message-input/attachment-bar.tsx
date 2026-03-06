/**
 * Attachment Bar
 *
 * Custom hook and component for managing pending attachments
 * (images, text files, large pastes) in the message input.
 */

import { type Accessor, createSignal, type Setter } from 'solid-js'
import { processImageFile, processTextFile } from './attachments'
import {
  MAX_FILES,
  MAX_IMAGES,
  PASTE_LINE_THRESHOLD,
  type PendingFile,
  type PendingImage,
  type PendingPaste,
} from './types'

// ---------------------------------------------------------------------------
// Hook return type
// ---------------------------------------------------------------------------

export interface AttachmentState {
  pendingImages: Accessor<PendingImage[]>
  setPendingImages: Setter<PendingImage[]>
  pendingFiles: Accessor<PendingFile[]>
  setPendingFiles: Setter<PendingFile[]>
  pendingPastes: Accessor<PendingPaste[]>
  setPendingPastes: Setter<PendingPaste[]>
  expandedPasteIndex: Accessor<number | null>
  setExpandedPasteIndex: (fn: number | null | ((prev: number | null) => number | null)) => void
  addImages: (files: File[]) => Promise<void>
  addTextFiles: (files: File[]) => Promise<void>
  handlePaste: (e: ClipboardEvent) => void
  handleDrop: (e: DragEvent) => void
  isDragging: Accessor<boolean>
  setIsDragging: (v: boolean) => void
  removeImage: (i: number) => void
  removeFile: (i: number) => void
  removePaste: (i: number) => void
  togglePastePreview: (i: number) => void
  clearAll: () => { files: PendingFile[]; pastes: PendingPaste[] }
}

// ---------------------------------------------------------------------------
// Hook
// ---------------------------------------------------------------------------

export function createAttachmentState(): AttachmentState {
  const [pendingImages, setPendingImages] = createSignal<PendingImage[]>([])
  const [pendingFiles, setPendingFiles] = createSignal<PendingFile[]>([])
  const [pendingPastes, setPendingPastes] = createSignal<PendingPaste[]>([])
  const [expandedPasteIndex, setExpandedPasteIndex] = createSignal<number | null>(null)
  const [isDragging, setIsDragging] = createSignal(false)

  const addImages = async (files: File[]): Promise<void> => {
    const remaining = MAX_IMAGES - pendingImages().length
    if (remaining <= 0) return
    const results = await Promise.all(files.slice(0, remaining).map(processImageFile))
    const valid = results.filter((r): r is NonNullable<typeof r> => r !== null)
    if (valid.length > 0) setPendingImages((prev) => [...prev, ...valid])
  }

  const addTextFiles = async (files: File[]): Promise<void> => {
    const remaining = MAX_FILES - pendingFiles().length
    if (remaining <= 0) return
    const results = await Promise.all(files.slice(0, remaining).map(processTextFile))
    const valid = results.filter((r): r is NonNullable<typeof r> => r !== null)
    if (valid.length > 0) setPendingFiles((prev) => [...prev, ...valid])
  }

  const handlePaste = (e: ClipboardEvent): void => {
    const items = e.clipboardData?.items
    if (!items) return
    const imageFiles: File[] = []
    for (const item of items) {
      if (item.type.startsWith('image/')) {
        const file = item.getAsFile()
        if (file) imageFiles.push(file)
      }
    }
    if (imageFiles.length > 0) {
      e.preventDefault()
      addImages(imageFiles)
      return
    }
    const text = e.clipboardData?.getData('text/plain')
    if (text) {
      const lines = text.split('\n')
      if (lines.length > PASTE_LINE_THRESHOLD) {
        e.preventDefault()
        setPendingPastes((prev) => [...prev, { content: text, lineCount: lines.length }])
      }
    }
  }

  const handleDrop = (e: DragEvent): void => {
    e.preventDefault()
    setIsDragging(false)
    const files = e.dataTransfer?.files
    if (!files) return
    const all = Array.from(files)
    const imgs = all.filter((f) => f.type.startsWith('image/'))
    const txts = all.filter((f) => !f.type.startsWith('image/'))
    if (imgs.length > 0) addImages(imgs)
    if (txts.length > 0) addTextFiles(txts)
  }

  const removeImage = (i: number): void => {
    setPendingImages((p) => p.filter((_, x) => x !== i))
  }

  const removeFile = (i: number): void => {
    setPendingFiles((p) => p.filter((_, x) => x !== i))
  }

  const removePaste = (i: number): void => {
    setPendingPastes((p) => p.filter((_, x) => x !== i))
    if (expandedPasteIndex() === i) setExpandedPasteIndex(null)
  }

  const togglePastePreview = (i: number): void => {
    setExpandedPasteIndex((p) => (p === i ? null : i))
  }

  /** Clear all attachments and return the cleared files/pastes for message building */
  const clearAll = (): { files: PendingFile[]; pastes: PendingPaste[] } => {
    setPendingImages([])
    const files = pendingFiles()
    setPendingFiles([])
    const pastes = pendingPastes()
    setPendingPastes([])
    setExpandedPasteIndex(null)
    return { files, pastes }
  }

  return {
    pendingImages,
    setPendingImages,
    pendingFiles,
    setPendingFiles,
    pendingPastes,
    setPendingPastes,
    expandedPasteIndex,
    setExpandedPasteIndex,
    addImages,
    addTextFiles,
    handlePaste,
    handleDrop,
    isDragging,
    setIsDragging,
    removeImage,
    removeFile,
    removePaste,
    togglePastePreview,
    clearAll,
  }
}
