# Wiring Diagrams

This directory contains auto-generated wiring diagrams for the Remote Token Dispenser project.

## Generated Diagrams

- **`wiring-diagram.svg`** - Main wiring: ESP8266 ↔ Azkoyen Hopper U-II
- **`pinout-diagram.svg`** - Wemos D1 Mini pinout reference with used pins highlighted
- **`power-diagram.svg`** - Power supply connections and grounding

## Regenerating Diagrams

The diagrams are generated from Python code for version control and reproducibility.

### Prerequisites

```bash
pip install schemdraw
```

### Generate All Diagrams

```bash
python docs/generate_diagrams.py
```

### Output

```
✓ Generated docs/wiring-diagram.svg
✓ Generated docs/pinout-diagram.svg
✓ Generated docs/power-diagram.svg
```

## Using in Documentation

Embed in Markdown:
```markdown
![Wiring Diagram](docs/wiring-diagram.svg)
```

Or reference directly:
- [Wiring Diagram](wiring-diagram.svg)
- [Pinout Diagram](pinout-diagram.svg)
- [Power Diagram](power-diagram.svg)

## Advantages of Code-Based Diagrams

✅ **Version controlled** - Changes tracked in git
✅ **Reproducible** - Anyone can regenerate
✅ **Maintainable** - Update code, not binary files
✅ **SVG format** - Scales perfectly, embeds in docs
✅ **Professional** - Clean, consistent styling
