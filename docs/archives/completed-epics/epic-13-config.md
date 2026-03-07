# Epic 13: Configuration UI

> Settings, preferences, customization

---

## Goal

Build a comprehensive settings UI for configuring AVA's behavior, providers, and preferences.

---

## Prerequisites

- Epic 5.4 (Model Registry) - Model configurations

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 13.1 | Settings Schema | Define all configurable options | ~200 |
| 13.2 | Provider Settings | API keys, OAuth, model selection | ~300 |
| 13.3 | Behavior Settings | Permissions, limits, defaults | ~250 |
| 13.4 | UI Components | Settings modal, forms, validation | ~400 |

**Total:** ~1150 lines

---

## Settings Categories

### Provider Settings
| Setting | Type | Default |
|---------|------|---------|
| Default provider | select | anthropic |
| Default model | select | claude-sonnet-4 |
| API keys | secure input | - |
| OAuth tokens | managed | - |
| OpenRouter fallback | toggle | true |

### Behavior Settings
| Setting | Type | Default |
|---------|------|---------|
| Auto-permission rules | list | [] |
| Max tool calls per turn | number | 10 |
| Context compaction threshold | percent | 80 |
| Auto-save sessions | toggle | true |
| Git auto-commit | toggle | false |

### UI Settings
| Setting | Type | Default |
|---------|------|---------|
| Theme | select | system |
| Font size | number | 14 |
| Show token counts | toggle | true |
| Streaming speed | select | normal |

---

## Key Features

### Settings Schema
```typescript
interface SettingsSchema {
  provider: {
    default: LLMProvider
    model: string
    apiKeys: Record<LLMProvider, string>
    openRouterFallback: boolean
  }
  behavior: {
    permissionRules: PermissionRule[]
    maxToolCalls: number
    compactionThreshold: number
    autoSave: boolean
    gitAutoCommit: boolean
  }
  ui: {
    theme: 'light' | 'dark' | 'system'
    fontSize: number
    showTokens: boolean
    streamingSpeed: 'slow' | 'normal' | 'fast'
  }
}
```

### Settings Manager
```typescript
class SettingsManager {
  private settings: SettingsSchema
  private listeners = new Set<(settings: SettingsSchema) => void>()

  async load(): Promise<void> {
    const stored = await getPlatform().credentials.get('settings')
    this.settings = stored ? JSON.parse(stored) : DEFAULT_SETTINGS
  }

  async save(): Promise<void> {
    await getPlatform().credentials.set('settings', JSON.stringify(this.settings))
    this.notifyListeners()
  }

  get<K extends keyof SettingsSchema>(key: K): SettingsSchema[K] {
    return this.settings[key]
  }

  set<K extends keyof SettingsSchema>(key: K, value: SettingsSchema[K]): void {
    this.settings[key] = value
    this.save()
  }

  subscribe(listener: (settings: SettingsSchema) => void): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }
}
```

---

## UI Components

### Settings Modal Structure
```
┌─────────────────────────────────────┐
│ Settings                        [×] │
├─────────────────────────────────────┤
│ ┌─────────┐                         │
│ │Providers│ API Keys & OAuth        │
│ ├─────────┤                         │
│ │Behavior │ Permissions, limits     │
│ ├─────────┤                         │
│ │   UI    │ Theme, display          │
│ ├─────────┤                         │
│ │ About   │ Version, links          │
│ └─────────┘                         │
├─────────────────────────────────────┤
│              [Save] [Cancel]        │
└─────────────────────────────────────┘
```

---

## Acceptance Criteria

- [ ] All settings persist across restarts
- [ ] API keys stored securely
- [ ] OAuth tokens managed automatically
- [ ] Settings validation prevents invalid values
- [ ] UI reflects setting changes immediately
- [ ] Export/import settings capability
