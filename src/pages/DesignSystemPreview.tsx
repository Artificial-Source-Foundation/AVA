/**
 * Design System Preview Page
 *
 * Showcases all UI components across all themes.
 * Use this page to test and compare design variations.
 */

import {
  Heart,
  Monitor,
  Moon,
  Palette,
  Search,
  Send,
  Settings,
  Sparkles,
  Sun,
  Terminal,
  User,
} from 'lucide-solid'
import { type Component, createSignal, For } from 'solid-js'
import { AgentActivityPanel, FileOperationsPanel, MemoryPanel } from '../components/panels'
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
import { type Mode, type Theme, useTheme } from '../contexts/theme'

const themes: { value: Theme; label: string; icon: Component<{ class?: string }> }[] = [
  { value: 'glass', label: 'Glass', icon: Sparkles },
  { value: 'minimal', label: 'Minimal', icon: Palette },
  { value: 'terminal', label: 'Terminal', icon: Terminal },
  { value: 'soft', label: 'Soft', icon: Heart },
]

const modes: { value: Mode; label: string; icon: Component<{ class?: string }> }[] = [
  { value: 'light', label: 'Light', icon: Sun },
  { value: 'dark', label: 'Dark', icon: Moon },
  { value: 'system', label: 'System', icon: Monitor },
]

export const DesignSystemPreview: Component = () => {
  const { theme, mode, setTheme, setMode, resolvedMode } = useTheme()

  const [inputValue, setInputValue] = createSignal('')
  const [textareaValue, setTextareaValue] = createSignal('')
  const [toggleValue, setToggleValue] = createSignal(true)
  const [selectValue, setSelectValue] = createSignal('option1')

  const selectOptions = [
    { value: 'option1', label: 'Claude 3.5 Sonnet', description: 'Fast and intelligent' },
    { value: 'option2', label: 'Claude 3 Opus', description: 'Most capable' },
    { value: 'option3', label: 'Claude 3 Haiku', description: 'Quick responses' },
  ]

  return (
    <div class="min-h-screen bg-[var(--background)] transition-colors duration-[var(--duration-normal)]">
      {/* Header */}
      <header class="sticky top-0 z-[var(--z-sticky)] border-b border-[var(--border-subtle)] bg-[var(--surface)] glass">
        <div class="max-w-6xl mx-auto px-6 py-4">
          <div class="flex items-center justify-between">
            <div>
              <h1 class="text-2xl font-bold text-[var(--text-primary)]">Estela Design System</h1>
              <p class="text-sm text-[var(--text-secondary)]">
                Component preview across all themes
              </p>
            </div>

            <div class="flex items-center gap-4">
              {/* Theme Selector */}
              <div class="flex items-center gap-2 p-1 bg-[var(--surface-sunken)] rounded-[var(--radius-lg)]">
                <For each={themes}>
                  {(t) => (
                    <button
                      type="button"
                      onClick={() => setTheme(t.value)}
                      class={`
                        flex items-center gap-2 px-3 py-1.5 rounded-[var(--radius-md)]
                        text-sm font-medium
                        transition-all duration-[var(--duration-fast)]
                        ${
                          theme() === t.value
                            ? 'bg-[var(--accent)] text-white shadow-sm'
                            : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface)]'
                        }
                      `}
                    >
                      <t.icon class="h-4 w-4" />
                      {t.label}
                    </button>
                  )}
                </For>
              </div>

              {/* Mode Selector */}
              <div class="flex items-center gap-1 p-1 bg-[var(--surface-sunken)] rounded-[var(--radius-lg)]">
                <For each={modes}>
                  {(m) => (
                    <button
                      type="button"
                      onClick={() => setMode(m.value)}
                      class={`
                        p-2 rounded-[var(--radius-md)]
                        transition-all duration-[var(--duration-fast)]
                        ${
                          mode() === m.value
                            ? 'bg-[var(--accent)] text-white shadow-sm'
                            : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--surface)]'
                        }
                      `}
                      title={m.label}
                    >
                      <m.icon class="h-4 w-4" />
                    </button>
                  )}
                </For>
              </div>
            </div>
          </div>
        </div>
      </header>

      {/* Content */}
      <main class="max-w-6xl mx-auto px-6 py-8">
        <div class="space-y-12">
          {/* Current Theme Info */}
          <Card glass class="animate-fade-in">
            <div class="flex items-center gap-4">
              <div class="flex-1">
                <p class="text-sm text-[var(--text-secondary)]">Current Configuration</p>
                <p class="text-lg font-semibold text-[var(--text-primary)]">
                  Theme: <span class="text-[var(--accent)] capitalize">{theme()}</span> &middot;{' '}
                  Mode: <span class="text-[var(--accent)] capitalize">{resolvedMode()}</span>
                </p>
              </div>
              <Badge variant="success">Active</Badge>
            </div>
          </Card>

          {/* Buttons Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Buttons</h2>

            <Card>
              <div class="space-y-6">
                {/* Variants */}
                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Variants</p>
                  <div class="flex flex-wrap gap-3">
                    <Button variant="primary">Primary</Button>
                    <Button variant="secondary">Secondary</Button>
                    <Button variant="ghost">Ghost</Button>
                    <Button variant="danger">Danger</Button>
                    <Button variant="success">Success</Button>
                  </div>
                </div>

                {/* Sizes */}
                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Sizes</p>
                  <div class="flex flex-wrap items-center gap-3">
                    <Button size="sm">Small</Button>
                    <Button size="md">Medium</Button>
                    <Button size="lg">Large</Button>
                    <Button size="icon">
                      <Settings class="h-4 w-4" />
                    </Button>
                  </div>
                </div>

                {/* States */}
                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">States</p>
                  <div class="flex flex-wrap gap-3">
                    <Button loading>Loading</Button>
                    <Button disabled>Disabled</Button>
                    <Button icon={<Send class="h-4 w-4" />}>With Icon</Button>
                    <Button iconRight={<Sparkles class="h-4 w-4" />}>Icon Right</Button>
                  </div>
                </div>
              </div>
            </Card>
          </section>

          {/* Inputs Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Inputs</h2>

            <Card>
              <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
                <Input
                  label="Email"
                  placeholder="Enter your email"
                  value={inputValue()}
                  onValueChange={setInputValue}
                  icon={<User class="h-4 w-4" />}
                  description="We'll never share your email."
                />

                <Input label="Search" placeholder="Search..." icon={<Search class="h-4 w-4" />} />

                <Input
                  label="With Error"
                  placeholder="Enter value"
                  error="This field is required"
                  required
                />

                <Input label="Disabled" placeholder="Can't edit this" disabled />

                <div class="md:col-span-2">
                  <Textarea
                    label="Message"
                    placeholder="Type your message..."
                    value={textareaValue()}
                    onValueChange={setTextareaValue}
                    description="Markdown is supported."
                    rows={3}
                  />
                </div>

                <Select
                  label="Model"
                  placeholder="Select a model"
                  options={selectOptions}
                  value={selectValue()}
                  onChange={setSelectValue}
                  description="Choose your preferred AI model."
                />

                <Select
                  label="With Error"
                  placeholder="Select an option"
                  options={selectOptions}
                  error="Please select an option"
                  required
                />
              </div>
            </Card>
          </section>

          {/* Cards Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Cards</h2>

            <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
              <Card title="Basic Card" description="A simple card with title and description.">
                <p class="text-sm text-[var(--text-secondary)]">
                  This is the card content. You can put anything here.
                </p>
              </Card>

              <Card
                title="Interactive Card"
                description="Click me!"
                interactive
                onClick={() => alert('Card clicked!')}
              >
                <p class="text-sm text-[var(--text-secondary)]">
                  This card has hover effects and is clickable.
                </p>
              </Card>

              <Card title="Glass Card" description="With glassmorphism effect" glass>
                <p class="text-sm text-[var(--text-secondary)]">
                  This card uses the glass effect (theme-dependent).
                </p>
              </Card>

              <Card
                title="Card with Footer"
                description="Has a footer section"
                footer={
                  <div class="flex justify-end gap-2">
                    <Button variant="ghost" size="sm">
                      Cancel
                    </Button>
                    <Button size="sm">Save</Button>
                  </div>
                }
              >
                <p class="text-sm text-[var(--text-secondary)]">Card content goes here.</p>
              </Card>
            </div>
          </section>

          {/* Badges Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Badges</h2>

            <Card>
              <div class="space-y-4">
                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Variants</p>
                  <div class="flex flex-wrap gap-2">
                    <Badge>Default</Badge>
                    <Badge variant="secondary">Secondary</Badge>
                    <Badge variant="success">Success</Badge>
                    <Badge variant="warning">Warning</Badge>
                    <Badge variant="error">Error</Badge>
                    <Badge variant="info">Info</Badge>
                    <Badge variant="outline">Outline</Badge>
                  </div>
                </div>

                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Sizes</p>
                  <div class="flex flex-wrap items-center gap-2">
                    <Badge size="sm">Small</Badge>
                    <Badge size="md">Medium</Badge>
                    <Badge size="lg">Large</Badge>
                  </div>
                </div>

                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Dot Indicators</p>
                  <div class="flex flex-wrap items-center gap-4">
                    <div class="flex items-center gap-2">
                      <Badge variant="success" dot />
                      <span class="text-sm">Online</span>
                    </div>
                    <div class="flex items-center gap-2">
                      <Badge variant="error" dot />
                      <span class="text-sm">Busy</span>
                    </div>
                    <div class="flex items-center gap-2">
                      <Badge variant="warning" dot />
                      <span class="text-sm">Away</span>
                    </div>
                    <div class="flex items-center gap-2">
                      <Badge variant="secondary" dot />
                      <span class="text-sm">Offline</span>
                    </div>
                  </div>
                </div>
              </div>
            </Card>
          </section>

          {/* Avatars Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Avatars</h2>

            <Card>
              <div class="space-y-6">
                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Sizes</p>
                  <div class="flex flex-wrap items-end gap-4">
                    <Avatar size="xs" initials="XS" />
                    <Avatar size="sm" initials="SM" />
                    <Avatar size="md" initials="MD" />
                    <Avatar size="lg" initials="LG" />
                    <Avatar size="xl" initials="XL" />
                  </div>
                </div>

                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">With Status</p>
                  <div class="flex flex-wrap items-center gap-4">
                    <Avatar initials="ON" status="online" />
                    <Avatar initials="OF" status="offline" />
                    <Avatar initials="AW" status="away" />
                    <Avatar initials="BS" status="busy" />
                  </div>
                </div>

                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Shapes</p>
                  <div class="flex flex-wrap items-center gap-4">
                    <Avatar initials="CI" shape="circle" />
                    <Avatar initials="SQ" shape="square" />
                  </div>
                </div>
              </div>
            </Card>
          </section>

          {/* Toggles Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Toggles</h2>

            <Card>
              <div class="space-y-4">
                <Toggle
                  label="Enable notifications"
                  description="Receive push notifications for important updates."
                  checked={toggleValue()}
                  onChange={setToggleValue}
                />

                <Toggle
                  label="Dark mode"
                  checked={resolvedMode() === 'dark'}
                  onChange={(checked) => setMode(checked ? 'dark' : 'light')}
                />

                <div class="flex gap-8">
                  <Toggle label="Small" size="sm" defaultChecked />
                  <Toggle label="Medium" size="md" defaultChecked />
                  <Toggle label="Large" size="lg" defaultChecked />
                </div>

                <Toggle label="Disabled toggle" disabled />
              </div>
            </Card>
          </section>

          {/* Chat Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Chat Bubbles</h2>

            <Card padding="none">
              <div class="p-4 space-y-4 bg-[var(--background-subtle)] rounded-[var(--radius-xl)]">
                <ChatBubble role="user" avatarInitials="JD" timestamp="10:30 AM">
                  Hey! Can you help me with something?
                </ChatBubble>

                <ChatBubble role="assistant" avatarInitials="AI" timestamp="10:30 AM">
                  Of course! I'd be happy to help. What do you need assistance with?
                </ChatBubble>

                <ChatBubble role="user" avatarInitials="JD" timestamp="10:31 AM">
                  I'm trying to understand how the design system works. Can you explain the theme
                  architecture?
                </ChatBubble>

                <ChatBubble role="assistant" avatarInitials="AI" timestamp="10:31 AM">
                  The design system uses a three-layer token architecture: 1. **Primitives** - Raw
                  values like colors and sizes 2. **Semantic** - Intent-based tokens like
                  `--text-primary` 3. **Component** - Component-specific overrides Each theme
                  (Glass, Minimal, Terminal, Soft) overrides these tokens to create different visual
                  styles while maintaining consistency.
                </ChatBubble>

                <ChatBubble role="system">Claude switched to Claude 3.5 Sonnet</ChatBubble>

                <TypingIndicator />
              </div>
            </Card>
          </section>

          {/* Panels Section */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Panels</h2>

            <div class="grid grid-cols-1 lg:grid-cols-3 gap-4">
              <Card padding="none" class="overflow-hidden">
                <div class="h-[400px]">
                  <AgentActivityPanel />
                </div>
              </Card>

              <Card padding="none" class="overflow-hidden">
                <div class="h-[400px]">
                  <FileOperationsPanel />
                </div>
              </Card>

              <Card padding="none" class="overflow-hidden">
                <div class="h-[400px]">
                  <MemoryPanel />
                </div>
              </Card>
            </div>
          </section>

          {/* Color Palette */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Color Palette</h2>

            <Card>
              <div class="space-y-6">
                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Surfaces</p>
                  <div class="grid grid-cols-5 gap-2">
                    <div
                      class="aspect-square rounded-lg bg-[var(--background)] border border-[var(--border-subtle)]"
                      title="background"
                    />
                    <div
                      class="aspect-square rounded-lg bg-[var(--surface)] border border-[var(--border-subtle)]"
                      title="surface"
                    />
                    <div
                      class="aspect-square rounded-lg bg-[var(--surface-raised)] border border-[var(--border-subtle)]"
                      title="surface-raised"
                    />
                    <div
                      class="aspect-square rounded-lg bg-[var(--surface-overlay)] border border-[var(--border-subtle)]"
                      title="surface-overlay"
                    />
                    <div
                      class="aspect-square rounded-lg bg-[var(--surface-sunken)] border border-[var(--border-subtle)]"
                      title="surface-sunken"
                    />
                  </div>
                </div>

                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Accents</p>
                  <div class="grid grid-cols-5 gap-2">
                    <div
                      class="aspect-square rounded-lg bg-[var(--accent-subtle)]"
                      title="accent-subtle"
                    />
                    <div
                      class="aspect-square rounded-lg bg-[var(--accent-muted)]"
                      title="accent-muted"
                    />
                    <div class="aspect-square rounded-lg bg-[var(--accent)]" title="accent" />
                    <div
                      class="aspect-square rounded-lg bg-[var(--accent-hover)]"
                      title="accent-hover"
                    />
                    <div
                      class="aspect-square rounded-lg bg-[var(--accent-active)]"
                      title="accent-active"
                    />
                  </div>
                </div>

                <div>
                  <p class="text-sm text-[var(--text-secondary)] mb-3">Feedback</p>
                  <div class="grid grid-cols-4 gap-2">
                    <div class="flex flex-col gap-1">
                      <div class="aspect-square rounded-lg bg-[var(--success)]" />
                      <span class="text-xs text-center text-[var(--text-tertiary)]">Success</span>
                    </div>
                    <div class="flex flex-col gap-1">
                      <div class="aspect-square rounded-lg bg-[var(--warning)]" />
                      <span class="text-xs text-center text-[var(--text-tertiary)]">Warning</span>
                    </div>
                    <div class="flex flex-col gap-1">
                      <div class="aspect-square rounded-lg bg-[var(--error)]" />
                      <span class="text-xs text-center text-[var(--text-tertiary)]">Error</span>
                    </div>
                    <div class="flex flex-col gap-1">
                      <div class="aspect-square rounded-lg bg-[var(--info)]" />
                      <span class="text-xs text-center text-[var(--text-tertiary)]">Info</span>
                    </div>
                  </div>
                </div>
              </div>
            </Card>
          </section>

          {/* Typography */}
          <section class="space-y-4">
            <h2 class="text-xl font-semibold text-[var(--text-primary)]">Typography</h2>

            <Card>
              <div class="space-y-4">
                <div class="text-5xl font-bold">Heading 1</div>
                <div class="text-4xl font-bold">Heading 2</div>
                <div class="text-3xl font-semibold">Heading 3</div>
                <div class="text-2xl font-semibold">Heading 4</div>
                <div class="text-xl font-medium">Heading 5</div>
                <div class="text-lg font-medium">Heading 6</div>
                <div class="text-base">
                  Body text - The quick brown fox jumps over the lazy dog.
                </div>
                <div class="text-sm text-[var(--text-secondary)]">
                  Small text - The quick brown fox jumps over the lazy dog.
                </div>
                <div class="text-xs text-[var(--text-tertiary)]">
                  Extra small text - The quick brown fox jumps over the lazy dog.
                </div>
                <div class="font-mono text-sm">Monospace: const greeting = "Hello, World!";</div>
              </div>
            </Card>
          </section>
        </div>
      </main>

      {/* Footer */}
      <footer class="border-t border-[var(--border-subtle)] mt-12">
        <div class="max-w-6xl mx-auto px-6 py-6">
          <p class="text-sm text-[var(--text-tertiary)] text-center">
            Estela Design System &middot; Built with SolidJS, Kobalte, and Tailwind CSS 4
          </p>
        </div>
      </footer>
    </div>
  )
}
