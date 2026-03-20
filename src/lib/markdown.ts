/**
 * Markdown Renderer
 *
 * Configures markdown-it for chat messages with:
 * - Fenced code blocks with language labels + copy buttons
 * - Syntax highlighting via lightweight regex highlighter
 * - DOMPurify sanitization (XSS prevention)
 * - Streaming-safe rendering (handles unterminated fences)
 */

import DOMPurify from 'dompurify'
import MarkdownIt from 'markdown-it'
import { highlightCode } from './syntax-highlight'

// ============================================================================
// Initialize markdown-it
// ============================================================================

const md = new MarkdownIt({
  html: false, // No raw HTML (security)
  linkify: true, // Auto-detect URLs
  typographer: false, // No smart quotes (code context)
  breaks: true, // Convert \n to <br> in paragraphs
  highlight(str: string, lang: string): string {
    const language = lang.trim().toLowerCase()
    const highlighted = language ? highlightCode(str, language) : escapeHtml(str)
    const displayLang = language || 'text'

    // Detect a file path in the language hint (e.g. ```typescript:src/foo.ts or ```src/foo.ts)
    const colonIdx = lang.indexOf(':')
    const filePath = colonIdx >= 0 ? lang.slice(colonIdx + 1).trim() : ''
    const applyBtn = filePath
      ? `<button type="button" class="code-apply-btn" data-apply-code data-file-path="${escapeHtml(filePath)}" title="Apply to ${escapeHtml(filePath)}" aria-label="Apply to file">` +
        `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">` +
        `<path d="M12 5v14"/><path d="M5 12l7 7 7-7"/>` +
        `</svg>` +
        `<span class="code-apply-label">Apply</span>` +
        `</button>`
      : ''

    return (
      `<div class="code-block-wrapper">` +
      `<div class="code-header">` +
      `<span class="code-lang">${escapeHtml(displayLang)}</span>` +
      applyBtn +
      `<button type="button" class="code-copy-btn" data-copy-code aria-label="Copy code">` +
      `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">` +
      `<rect x="9" y="9" width="13" height="13" rx="2" ry="2"/>` +
      `<path d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/>` +
      `</svg>` +
      `<span class="code-copy-label">Copy</span>` +
      `</button>` +
      `</div>` +
      `<pre><code class="language-${escapeHtml(displayLang)}">${highlighted}</code></pre>` +
      `</div>`
    )
  },
})

// Override link rendering to add target="_blank" and rel="noopener"
const defaultLinkOpen =
  md.renderer.rules.link_open ||
  ((tokens, idx, options, _env, self) => self.renderToken(tokens, idx, options))

md.renderer.rules.link_open = (tokens, idx, options, env, self) => {
  tokens[idx].attrSet('target', '_blank')
  tokens[idx].attrSet('rel', 'noopener noreferrer')
  return defaultLinkOpen(tokens, idx, options, env, self)
}

// ============================================================================
// Helpers
// ============================================================================

function escapeHtml(str: string): string {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
}

/** Detect unterminated code fence in streaming content */
function hasUnterminatedFence(content: string): boolean {
  const fenceRegex = /^(`{3,}|~{3,})/gm
  let open = false
  let match: RegExpExecArray | null = fenceRegex.exec(content)
  while (match !== null) {
    open = !open
    match = fenceRegex.exec(content)
  }
  return open
}

// Configure DOMPurify
const PURIFY_CONFIG = {
  RETURN_TRUSTED_TYPE: false as const,
  ALLOWED_TAGS: [
    'p',
    'br',
    'strong',
    'em',
    'del',
    'code',
    'pre',
    'blockquote',
    'h1',
    'h2',
    'h3',
    'h4',
    'h5',
    'h6',
    'ul',
    'ol',
    'li',
    'table',
    'thead',
    'tbody',
    'tr',
    'th',
    'td',
    'a',
    'span',
    'div',
    'button',
    'svg',
    'rect',
    'path',
    'hr',
  ],
  ALLOWED_ATTR: [
    'class',
    'href',
    'target',
    'rel',
    'title',
    'data-copy-code',
    'aria-label',
    'aria-hidden',
    'type',
    'width',
    'height',
    'viewBox',
    'fill',
    'stroke',
    'stroke-width',
    'stroke-linecap',
    'stroke-linejoin',
    'x',
    'y',
    'rx',
    'ry',
    'd',
  ],
  ALLOW_DATA_ATTR: true,
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Render markdown to sanitized HTML (for completed messages)
 */
export function renderMarkdown(content: string): string {
  if (!content) return ''
  const html = md.render(content)
  return DOMPurify.sanitize(html, PURIFY_CONFIG)
}

/**
 * Render markdown during streaming (handles unterminated code fences)
 */
export function renderMarkdownStreaming(content: string): string {
  if (!content) return ''

  let safeContent = content
  if (hasUnterminatedFence(content)) {
    // Close the open fence so markdown-it produces valid HTML
    safeContent += '\n```'
  }

  const html = md.render(safeContent)
  return DOMPurify.sanitize(html, PURIFY_CONFIG)
}
