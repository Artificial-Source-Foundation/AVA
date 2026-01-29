# Delta9 Configuration Reference

> Complete reference for all Delta9 configuration options

---

## Configuration Files

Delta9 loads configuration from multiple sources (in order of priority):

1. **Defaults**: Hardcoded sensible defaults
2. **Global**: `~/.config/opencode/delta9.json`
3. **Project**: `.delta9/config.json` (overrides global)

Project configuration takes precedence, allowing per-project customization.

---

## Quick Start

Minimal configuration to get started:

```json
{
  "council": {
    "enabled": true,
    "defaultMode": "standard"
  }
}
```

Full configuration with all defaults shown below.

---

## Commander Configuration

Controls the lead planner agent.

```json
{
  "commander": {
    "model": "anthropic/claude-sonnet-4",
    "temperature": 0.7,
    "dispatchModel": "anthropic/claude-sonnet-4"
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `anthropic/claude-sonnet-4` | Model for strategic planning |
| `temperature` | number | `0.7` | Temperature (0-2) for planning creativity |
| `dispatchModel` | string | `anthropic/claude-sonnet-4` | Model for task dispatch coordination |

---

## Council Configuration

Controls the Oracle council (The Delta Team).

```json
{
  "council": {
    "enabled": true,
    "defaultMode": "standard",
    "autoDetectComplexity": true,
    "members": [
      {
        "name": "Cipher",
        "model": "anthropic/claude-opus-4-5",
        "enabled": true,
        "specialty": "architecture",
        "temperature": 0.2
      },
      {
        "name": "Vector",
        "model": "openai/gpt-4o",
        "enabled": true,
        "specialty": "logic",
        "temperature": 0.4
      },
      {
        "name": "Prism",
        "model": "google/gemini-2.0-flash",
        "enabled": true,
        "specialty": "ui",
        "temperature": 0.6
      },
      {
        "name": "Apex",
        "model": "deepseek/deepseek-chat",
        "enabled": true,
        "specialty": "performance",
        "temperature": 0.3
      }
    ],
    "parallel": true,
    "requireConsensus": false,
    "minResponses": 2,
    "timeoutSeconds": 120
  }
}
```

### Council Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable council system |
| `defaultMode` | string | `standard` | Default mode: `none`, `quick`, `standard`, `xhigh` |
| `autoDetectComplexity` | boolean | `true` | Auto-detect council mode from task complexity |
| `parallel` | boolean | `true` | Run oracles in parallel |
| `requireConsensus` | boolean | `false` | Require oracle consensus before proceeding |
| `minResponses` | number | `2` | Minimum oracle responses required (1-10) |
| `timeoutSeconds` | number | `120` | Oracle response timeout (10-600 seconds) |

### Oracle (Member) Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `name` | string | - | Oracle display name (Cipher, Vector, Prism, Apex) |
| `model` | string | - | Model to use (any OpenCode-supported model) |
| `enabled` | boolean | `true` | Whether this oracle is active |
| `specialty` | string | `general` | Specialty: `architecture`, `logic`, `ui`, `performance`, `general` |
| `temperature` | number | varies | Oracle personality temperature (0-2) |

### Council Modes

| Mode | Description |
|------|-------------|
| `none` | Commander only, no council involvement |
| `quick` | Single oracle (Cipher) for moderate tasks |
| `standard` | All enabled oracles for complex tasks |
| `xhigh` | All oracles + Scout/Intel reconnaissance |

---

## Operators Configuration

Controls task execution agents.

```json
{
  "operators": {
    "defaultModel": "anthropic/claude-sonnet-4",
    "complexModel": "anthropic/claude-opus-4-5",
    "maxParallel": 3,
    "retryLimit": 2,
    "canInvokeSupport": true
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `defaultModel` | string | `anthropic/claude-sonnet-4` | Model for standard tasks |
| `complexModel` | string | `anthropic/claude-opus-4-5` | Model for complex multi-file tasks |
| `maxParallel` | number | `3` | Maximum parallel operators (1-10) |
| `retryLimit` | number | `2` | Retry attempts on failure (0-5) |
| `canInvokeSupport` | boolean | `true` | Operators can call support agents |

---

## Validator Configuration

Controls the QA validation gate.

```json
{
  "validator": {
    "model": "anthropic/claude-haiku-4",
    "strictMode": false,
    "runTests": true,
    "checkLinting": true
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `anthropic/claude-haiku-4` | Model for validation |
| `strictMode` | boolean | `false` | Enable thorough validation |
| `runTests` | boolean | `true` | Run tests during validation |
| `checkLinting` | boolean | `true` | Check linting during validation |

---

## Patcher Configuration

Controls quick-fix agent for validation failures.

```json
{
  "patcher": {
    "model": "anthropic/claude-haiku-4",
    "maxLines": 50
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `anthropic/claude-haiku-4` | Model for patches |
| `maxLines` | number | `50` | Maximum lines patcher can change (1-500) |

---

## Support Agents Configuration

Controls specialized support agents.

```json
{
  "support": {
    "scout": {
      "model": "anthropic/claude-haiku-4",
      "timeoutSeconds": 30
    },
    "intel": {
      "model": "anthropic/claude-sonnet-4",
      "sources": ["docs", "github", "web"]
    },
    "strategist": {
      "model": "openai/gpt-4o",
      "invokeThreshold": "complex"
    },
    "uiOps": {
      "model": "google/gemini-2.0-flash",
      "styleSystem": "tailwind"
    },
    "scribe": {
      "model": "google/gemini-2.0-flash",
      "format": "markdown"
    },
    "optics": {
      "model": "google/gemini-2.0-flash"
    },
    "qa": {
      "model": "anthropic/claude-sonnet-4",
      "frameworkDetect": true
    }
  }
}
```

### Scout (SCOUT)

Fast codebase search agent.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `anthropic/claude-haiku-4` | Model for search |
| `timeoutSeconds` | number | `30` | Search timeout (5-120 seconds) |

### Intel (INTEL)

Documentation and research agent.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `anthropic/claude-sonnet-4` | Model for research |
| `sources` | array | `["docs", "github", "web"]` | Information sources |

Sources: `docs` (project docs), `github` (GitHub), `web` (web search)

### Strategist (STRATEGIST)

Mid-execution advisor agent.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `openai/gpt-4o` | Model for advice |
| `invokeThreshold` | string | `complex` | When to invoke: `simple`, `moderate`, `complex` |

### UI-Ops (FACADE)

Frontend specialist agent.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `google/gemini-2.0-flash` | Model for UI work |
| `styleSystem` | string | `tailwind` | Style system: `tailwind`, `css`, `scss`, `styled-components` |

### Scribe (SCRIBE)

Documentation writer agent.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `google/gemini-2.0-flash` | Model for docs |
| `format` | string | `markdown` | Doc format: `markdown`, `jsdoc`, `tsdoc` |

### Optics (SPECTRE)

Vision/multimodal agent.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `google/gemini-2.0-flash` | Model for vision tasks |

### QA (SENTINEL)

Dedicated test writer agent.

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `model` | string | `anthropic/claude-sonnet-4` | Model for test writing |
| `frameworkDetect` | boolean | `true` | Auto-detect test framework |

---

## Mission Configuration

Controls mission state management.

```json
{
  "mission": {
    "autoCheckpoint": true,
    "checkpointOn": "objective_complete",
    "stateDir": ".delta9",
    "historyEnabled": true
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `autoCheckpoint` | boolean | `true` | Create checkpoints automatically |
| `checkpointOn` | string | `objective_complete` | When to checkpoint: `objective_complete`, `task_complete`, `never` |
| `stateDir` | string | `.delta9` | Directory for state files |
| `historyEnabled` | boolean | `true` | Enable action history logging |

---

## Memory Configuration

Controls cross-session learning.

```json
{
  "memory": {
    "enabled": true,
    "learnFromFailures": true,
    "learnFromSuccesses": true,
    "maxEntries": 1000
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable memory system |
| `learnFromFailures` | boolean | `true` | Learn from failed tasks |
| `learnFromSuccesses` | boolean | `true` | Learn from successful tasks |
| `maxEntries` | number | `1000` | Maximum memory entries (10-10000) |

---

## Budget Configuration

Controls cost tracking and limits.

```json
{
  "budget": {
    "enabled": true,
    "defaultLimit": 10.0,
    "warnAt": 0.7,
    "pauseAt": 0.9,
    "trackByAgent": true
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable budget tracking |
| `defaultLimit` | number | `10.0` | Default budget in dollars (0.01-1000) |
| `warnAt` | number | `0.7` | Warning threshold (0-1, percentage) |
| `pauseAt` | number | `0.9` | Pause threshold (0-1, percentage) |
| `trackByAgent` | boolean | `true` | Track costs per agent type |

---

## Notification Configuration

Controls external notifications.

```json
{
  "notifications": {
    "enabled": false,
    "discordWebhook": null,
    "slackWebhook": null,
    "onEvents": ["mission_complete", "validation_failed", "budget_warning", "needs_input"]
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable notifications |
| `discordWebhook` | string\|null | `null` | Discord webhook URL |
| `slackWebhook` | string\|null | `null` | Slack webhook URL |
| `onEvents` | array | all | Events to notify on |

Events: `mission_complete`, `validation_failed`, `budget_warning`, `needs_input`

---

## UI Configuration

Controls user interface behavior.

```json
{
  "ui": {
    "showProgress": true,
    "showCost": true,
    "verboseLogs": false
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `showProgress` | boolean | `true` | Show progress indicators |
| `showCost` | boolean | `true` | Show cost tracking |
| `verboseLogs` | boolean | `false` | Enable verbose logging |

---

## Seamless Integration Configuration

Controls how Delta9 integrates with OpenCode.

```json
{
  "seamless": {
    "replaceBuild": true,
    "replacePlan": true,
    "keywordDetection": true,
    "keywords": {
      "councilXhigh": ["thorough", "careful", "critical", "important"],
      "councilNone": ["quick", "just", "simple", "fast"],
      "forcePlan": ["plan", "design", "architect", "strategy"]
    }
  }
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `replaceBuild` | boolean | `true` | Replace default build agent |
| `replacePlan` | boolean | `true` | Replace default plan agent |
| `keywordDetection` | boolean | `true` | Enable keyword-based mode detection |

### Keywords

| Keyword Group | Default Keywords | Effect |
|---------------|------------------|--------|
| `councilXhigh` | thorough, careful, critical, important | Force XHIGH mode |
| `councilNone` | quick, just, simple, fast | Force NONE mode |
| `forcePlan` | plan, design, architect, strategy | Force planning phase |

---

## Full Default Configuration

Complete configuration with all defaults:

```json
{
  "commander": {
    "model": "anthropic/claude-sonnet-4",
    "temperature": 0.7,
    "dispatchModel": "anthropic/claude-sonnet-4"
  },
  "council": {
    "enabled": true,
    "defaultMode": "standard",
    "autoDetectComplexity": true,
    "members": [
      {
        "name": "Cipher",
        "model": "anthropic/claude-opus-4-5",
        "enabled": true,
        "specialty": "architecture",
        "temperature": 0.2
      },
      {
        "name": "Vector",
        "model": "openai/gpt-4o",
        "enabled": true,
        "specialty": "logic",
        "temperature": 0.4
      },
      {
        "name": "Prism",
        "model": "google/gemini-2.0-flash",
        "enabled": true,
        "specialty": "ui",
        "temperature": 0.6
      },
      {
        "name": "Apex",
        "model": "deepseek/deepseek-chat",
        "enabled": true,
        "specialty": "performance",
        "temperature": 0.3
      }
    ],
    "parallel": true,
    "requireConsensus": false,
    "minResponses": 2,
    "timeoutSeconds": 120
  },
  "operators": {
    "defaultModel": "anthropic/claude-sonnet-4",
    "complexModel": "anthropic/claude-opus-4-5",
    "maxParallel": 3,
    "retryLimit": 2,
    "canInvokeSupport": true
  },
  "validator": {
    "model": "anthropic/claude-haiku-4",
    "strictMode": false,
    "runTests": true,
    "checkLinting": true
  },
  "patcher": {
    "model": "anthropic/claude-haiku-4",
    "maxLines": 50
  },
  "support": {
    "scout": {
      "model": "anthropic/claude-haiku-4",
      "timeoutSeconds": 30
    },
    "intel": {
      "model": "anthropic/claude-sonnet-4",
      "sources": ["docs", "github", "web"]
    },
    "strategist": {
      "model": "openai/gpt-4o",
      "invokeThreshold": "complex"
    },
    "uiOps": {
      "model": "google/gemini-2.0-flash",
      "styleSystem": "tailwind"
    },
    "scribe": {
      "model": "google/gemini-2.0-flash",
      "format": "markdown"
    },
    "optics": {
      "model": "google/gemini-2.0-flash"
    },
    "qa": {
      "model": "anthropic/claude-sonnet-4",
      "frameworkDetect": true
    }
  },
  "mission": {
    "autoCheckpoint": true,
    "checkpointOn": "objective_complete",
    "stateDir": ".delta9",
    "historyEnabled": true
  },
  "memory": {
    "enabled": true,
    "learnFromFailures": true,
    "learnFromSuccesses": true,
    "maxEntries": 1000
  },
  "budget": {
    "enabled": true,
    "defaultLimit": 10.0,
    "warnAt": 0.7,
    "pauseAt": 0.9,
    "trackByAgent": true
  },
  "notifications": {
    "enabled": false,
    "discordWebhook": null,
    "slackWebhook": null,
    "onEvents": ["mission_complete", "validation_failed", "budget_warning", "needs_input"]
  },
  "ui": {
    "showProgress": true,
    "showCost": true,
    "verboseLogs": false
  },
  "seamless": {
    "replaceBuild": true,
    "replacePlan": true,
    "keywordDetection": true,
    "keywords": {
      "councilXhigh": ["thorough", "careful", "critical", "important"],
      "councilNone": ["quick", "just", "simple", "fast"],
      "forcePlan": ["plan", "design", "architect", "strategy"]
    }
  }
}
```

---

## Model Providers

Delta9 supports any model available through OpenCode:

### Anthropic
- `anthropic/claude-opus-4-5` (recommended for council)
- `anthropic/claude-sonnet-4` (recommended for operators)
- `anthropic/claude-haiku-4` (recommended for validator/patcher)

### OpenAI
- `openai/gpt-4o`
- `openai/gpt-4-turbo`
- `openai/gpt-3.5-turbo`

### Google
- `google/gemini-2.0-flash`
- `google/gemini-1.5-pro`

### DeepSeek
- `deepseek/deepseek-chat`
- `deepseek/deepseek-coder`

### Others

See [OpenCode model documentation](https://opencode.ai/docs/models) for full list.

---

## Environment Variables

Some settings can be overridden via environment variables:

| Variable | Description |
|----------|-------------|
| `DELTA9_STATE_DIR` | Override state directory |
| `DELTA9_DEBUG` | Enable debug logging |
| `DELTA9_BUDGET_LIMIT` | Override budget limit |

---

## See Also

- [User Guide](USER_GUIDE.md) - Usage guide
- [Examples](EXAMPLES.md) - Example configurations
- [Specification](spec.md) - Full technical specification
