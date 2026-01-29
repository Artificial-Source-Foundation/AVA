# Delta9 Architecture

Delta9-specific documentation for the Commander + Council + Operators system.

## Files

| File | Description |
|------|-------------|
| [architecture.md](architecture.md) | System design with diagrams |
| [agents.md](agents.md) | Agent roster (Commander, Council, Operators) |
| [api.md](api.md) | Internal API reference |
| [development.md](development.md) | Development workflow |
| [research.md](research.md) | AI documentation research |

## Key Concepts

- **Commander**: Lead planner, never writes code
- **Council**: 1-4 heterogeneous Oracles
- **Operators**: Task executors (Sonnet 4)
- **Validator**: QA gate (Haiku)
- **Mission State**: Persisted in `.delta9/mission.json`
