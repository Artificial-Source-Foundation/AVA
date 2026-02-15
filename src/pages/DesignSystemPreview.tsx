/**
 * Design System Preview Page
 *
 * Showcases all UI components in the dark theme.
 * Access at: http://localhost:1420/?preview=true
 */

import { Heart, Send, Settings, Sparkles, Terminal, User } from 'lucide-solid'
import { type Component, createSignal } from 'solid-js'
import {
  Avatar,
  Badge,
  Button,
  Card,
  ChatBubble,
  Input,
  Select,
  Textarea,
  Toggle,
  TypingIndicator,
} from '../components/ui'

export const DesignSystemPreview: Component = () => {
  const [inputValue, setInputValue] = createSignal('')
  const [textareaValue, setTextareaValue] = createSignal('')
  const [toggleValue, setToggleValue] = createSignal(true)
  const [selectValue, setSelectValue] = createSignal('option1')

  const selectOptions = [
    { value: 'option1', label: 'Claude 3.5 Sonnet', description: 'Fast and intelligent' },
    { value: 'option2', label: 'Claude 3 Opus', description: 'Most capable' },
    { value: 'option3', label: 'GPT-4o', description: 'OpenAI flagship' },
  ]

  return (
    <div class="min-h-screen bg-[var(--background)] text-[var(--text-primary)] p-8">
      <div class="max-w-6xl mx-auto space-y-12">
        {/* Header */}
        <div class="text-center space-y-4">
          <div class="flex items-center justify-center gap-3">
            <div class="w-12 h-12 rounded-xl bg-[var(--accent)] flex items-center justify-center">
              <Sparkles class="w-6 h-6 text-white" />
            </div>
            <h1 class="text-3xl font-bold">AVA Design System</h1>
          </div>
          <p class="text-[var(--text-secondary)]">Component library preview - Dark theme</p>
        </div>

        {/* Buttons */}
        <section class="space-y-4">
          <h2 class="text-xl font-semibold border-b border-[var(--border-subtle)] pb-2">Buttons</h2>
          <div class="flex flex-wrap gap-4">
            <Button variant="primary">
              <Send class="w-4 h-4" />
              Primary
            </Button>
            <Button variant="secondary">
              <Settings class="w-4 h-4" />
              Secondary
            </Button>
            <Button variant="ghost">
              <Heart class="w-4 h-4" />
              Ghost
            </Button>
            <Button variant="primary" disabled>
              Disabled
            </Button>
          </div>
          <div class="flex flex-wrap gap-4">
            <Button size="sm" variant="primary">
              Small
            </Button>
            <Button size="md" variant="primary">
              Medium
            </Button>
            <Button size="lg" variant="primary">
              Large
            </Button>
          </div>
        </section>

        {/* Inputs */}
        <section class="space-y-4">
          <h2 class="text-xl font-semibold border-b border-[var(--border-subtle)] pb-2">Inputs</h2>
          <div class="grid grid-cols-1 md:grid-cols-2 gap-4 max-w-2xl">
            <Input
              placeholder="Search..."
              value={inputValue()}
              onValueChange={setInputValue}
              type="search"
            />
            <Input placeholder="Disabled input" disabled />
            <div class="md:col-span-2">
              <Textarea
                placeholder="Write your message..."
                value={textareaValue()}
                onValueChange={setTextareaValue}
                rows={3}
              />
            </div>
          </div>
        </section>

        {/* Select & Toggle */}
        <section class="space-y-4">
          <h2 class="text-xl font-semibold border-b border-[var(--border-subtle)] pb-2">
            Select & Toggle
          </h2>
          <div class="flex flex-wrap items-center gap-6">
            <div class="w-64">
              <Select
                options={selectOptions}
                value={selectValue()}
                onChange={setSelectValue}
                placeholder="Select model"
              />
            </div>
            <div class="flex items-center gap-3">
              <Toggle checked={toggleValue()} onChange={setToggleValue} />
              <span class="text-sm text-[var(--text-secondary)]">
                {toggleValue() ? 'Enabled' : 'Disabled'}
              </span>
            </div>
          </div>
        </section>

        {/* Avatars & Badges */}
        <section class="space-y-4">
          <h2 class="text-xl font-semibold border-b border-[var(--border-subtle)] pb-2">
            Avatars & Badges
          </h2>
          <div class="flex flex-wrap items-center gap-4">
            <Avatar size="sm" initials="U" />
            <Avatar size="md" initials="AI" />
            <Avatar size="lg" initials="ES" />
            <Badge variant="default">Default</Badge>
            <Badge variant="success">Success</Badge>
            <Badge variant="warning">Warning</Badge>
            <Badge variant="error">Error</Badge>
          </div>
        </section>

        {/* Cards */}
        <section class="space-y-4">
          <h2 class="text-xl font-semibold border-b border-[var(--border-subtle)] pb-2">Cards</h2>
          <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
            <Card>
              <div class="flex items-center gap-3 mb-3">
                <div class="w-10 h-10 rounded-lg bg-[var(--accent)] flex items-center justify-center">
                  <Terminal class="w-5 h-5 text-white" />
                </div>
                <div>
                  <h3 class="font-medium">Terminal</h3>
                  <p class="text-xs text-[var(--text-tertiary)]">Execute commands</p>
                </div>
              </div>
              <p class="text-sm text-[var(--text-secondary)]">
                Run shell commands and see output in real-time.
              </p>
            </Card>
            <Card>
              <div class="flex items-center gap-3 mb-3">
                <div class="w-10 h-10 rounded-lg bg-[var(--success)] flex items-center justify-center">
                  <User class="w-5 h-5 text-white" />
                </div>
                <div>
                  <h3 class="font-medium">Agents</h3>
                  <p class="text-xs text-[var(--text-tertiary)]">AI assistants</p>
                </div>
              </div>
              <p class="text-sm text-[var(--text-secondary)]">
                Specialized agents for different tasks.
              </p>
            </Card>
            <Card>
              <div class="flex items-center gap-3 mb-3">
                <div class="w-10 h-10 rounded-lg bg-[var(--warning)] flex items-center justify-center">
                  <Settings class="w-5 h-5 text-white" />
                </div>
                <div>
                  <h3 class="font-medium">Settings</h3>
                  <p class="text-xs text-[var(--text-tertiary)]">Configure app</p>
                </div>
              </div>
              <p class="text-sm text-[var(--text-secondary)]">
                Customize providers, keybindings, and more.
              </p>
            </Card>
          </div>
        </section>

        {/* Chat Bubbles */}
        <section class="space-y-4">
          <h2 class="text-xl font-semibold border-b border-[var(--border-subtle)] pb-2">
            Chat Bubbles
          </h2>
          <div class="space-y-4 max-w-2xl">
            {/* biome-ignore lint/a11y/useValidAriaRole: role is a component prop, not ARIA */}
            <ChatBubble role="user">How do I create a new React component?</ChatBubble>
            {/* biome-ignore lint/a11y/useValidAriaRole: role is a component prop, not ARIA */}
            <ChatBubble role="assistant">
              I'll help you create a new React component. First, let's understand what type of
              component you need...
            </ChatBubble>
            <TypingIndicator />
          </div>
        </section>
      </div>
    </div>
  )
}
