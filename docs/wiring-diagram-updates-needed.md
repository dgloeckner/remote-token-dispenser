# Wiring Diagram Updates Needed

**Purpose:** Document required SVG annotations for critical hardware configuration requirements discovered during debugging.

**Context:** The existing wiring diagrams don't show two critical requirements:
1. Hopper DIP switch MUST be in NEGATIVE mode (active LOW control)
2. PC817 optocoupler #1 R1 resistor needs 330Ω parallel modification for proper 13.3mA drive current

**Reference:** `docs/azkoyen-hopper-protocol.md` section 2.1 for voltage thresholds and mode specifications.

---

## Files to Update

- `docs/wiring-diagram-optocoupler.svg`
- `docs/pinout-diagram-optocoupler.svg`

---

## ⚠️ CRITICAL: Fix Existing Errors First

**Before adding new annotations, these existing errors MUST be corrected:**

### wiring-diagram-optocoupler.svg

**Line 311-312: INCORRECT BOM text (German + wrong info)**
```svg
<!-- WRONG: -->
<text x="56" y="598" font-size="9" fill="#64748b">Keine zusätzlichen Widerstände</text>
<text x="56" y="616" font-size="9" fill="#64748b">benötigt!</text>

<!-- CORRECT: -->
<text x="56" y="598" font-size="9" fill="#f97316">1× 330Ω resistor (1/4W)</text>
<text x="56" y="616" font-size="9" fill="#fb923c">   For R1 mod on Opto #1 ⚠️</text>
```

**Line 156-158: INCORRECT VCC annotation**
```svg
<!-- WRONG: -->
<text x="550" y="188" text-anchor="middle" font-size="8" font-weight="700" fill="#ef4444">✗ NC</text>
<text x="550" y="232" text-anchor="middle" font-size="6" fill="#fca5a5">Do not connect!</text>
<text x="550" y="240" text-anchor="middle" font-size="5" fill="#94a3b8">Hopper has internal pull-up</text>

<!-- CORRECT: -->
<text x="550" y="188" text-anchor="middle" font-size="8" font-weight="700" fill="#ef4444">✓ 12V</text>
<text x="550" y="232" text-anchor="middle" font-size="6" fill="#4ade80">Connect to 12V VCC</text>
<text x="550" y="240" text-anchor="middle" font-size="5" fill="#94a3b8">Provides pull-up voltage</text>
```

**Line 123: Remove "VCC: NC!" from header**
```svg
<!-- WRONG: -->
<text x="520" y="141" text-anchor="middle" font-size="9" font-weight="700" fill="#4ade80">CH1 · MOTOR CONTROL — Optocoupler #1 · D1 → Pin 7 ✓ (VCC: NC!)</text>

<!-- CORRECT: -->
<text x="520" y="141" text-anchor="middle" font-size="9" font-weight="700" fill="#4ade80">CH1 · MOTOR CONTROL — Optocoupler #1 · D1 → Pin 7 ✓ (R1 mod required!)</text>
```

**Line 329: German text**
```svg
<!-- WRONG: -->
<text x="866" y="410" font-size="8" fill="#64748b">H Level pin nicht verbunden (N/C)</text>

<!-- CORRECT: -->
<text x="866" y="410" font-size="8" fill="#64748b">H Level pin not connected (N/C)</text>
```

### pinout-diagram-optocoupler.svg

**Line 17: German subtitle**
```svg
<!-- WRONG: -->
<text x="360" y="54" text-anchor="middle" font-size="11" fill="#64748b">Token Dispenser mit PC817 Optokopplern · Invertierte Logik (LOW = aktiv)</text>

<!-- CORRECT: -->
<text x="360" y="54" text-anchor="middle" font-size="11" fill="#64748b">Token Dispenser with PC817 Optocouplers · Inverted Logic (LOW = active)</text>
```

**Line 146: German legend text**
```svg
<!-- WRONG: -->
<text x="628" y="491" font-size="9" fill="#fbbf24">⚠ LOW=aktiv</text>

<!-- CORRECT: -->
<text x="628" y="491" font-size="9" fill="#fbbf24">⚠ LOW=active</text>
```

---

## Required Annotations for `wiring-diagram-optocoupler.svg`

### 1. Optocoupler #1 (Motor Control) - R1 Modification Callout

**Location:** Near PC817 #1 module (motor control, around line 120-142 in SVG)

**Add warning box with:**
```
⚠️ CRITICAL MODIFICATION REQUIRED
Optocoupler #1 (Motor Control) R1 Resistor:
• Stock value: 1kΩ (provides only 3.3mA - TOO LOW!)
• Required: Add 330Ω resistor in PARALLEL
• Result: (1kΩ || 330Ω) = 248Ω → 13.3mA drive current ✓

Without this modification:
- Phototransistor won't saturate
- OUT stays at 3-7V instead of < 0.5V
- Motor control unreliable or non-functional

Reference: docs/azkoyen-hopper-protocol.md section 2.1
```

**Visual styling:**
- Background: Orange/amber (#f59e0b or #fb923c)
- Border: Solid 2px stroke, orange (#f97316)
- Text color: Dark text on light background for readability
- Position: Adjacent to optocoupler #1 box, connected with arrow or leader line

### 2. Hopper DIP Switch Configuration Callout

**Location:** Near Azkoyen Hopper U-II header (around line 73-79 in SVG) or in separate configuration note box

**Add warning box with:**
```
⚠️ HOPPER DIP SWITCH: NEGATIVE MODE REQUIRED

CRITICAL: Hopper MUST be configured in NEGATIVE mode
• Control signal: Active LOW (< 0.5V = motor ON, > 4V = motor OFF)
• DIP switch setting: STANDARD + NEGATIVE
• Location: Connector #6 on hopper control board

WRONG MODE SYMPTOMS:
• Motor doesn't engage during dispense
• Motor engages AFTER timeout instead of during dispense
• Completely inverted behavior

Voltage thresholds (NEGATIVE mode):
• Motor ON:  Control pin < 0.5V
• Motor OFF: Control pin 4V to Vcc ±10% (typically 4-13.2V for 12V)

Reference: docs/azkoyen-hopper-protocol.md section 2.1
```

**Visual styling:**
- Background: Red/crimson (#ef4444 or #dc2626)
- Border: Solid 2px stroke, dark red (#991b1b)
- Text color: White text on red background
- Position: Top right or bottom right corner, highly visible

### 3. Optocoupler #1 Wiring Annotation

**Location:** Near the D1 → IN+ connection (around line 145-154 in SVG)

**Add annotation text:**
```
D1 → IN+ Wiring (Not Inverted)
GPIO HIGH → LED ON → OUT LOW (< 0.5V) → Motor ON
Requires R1 modification for saturation!
```

**Visual styling:**
- Small annotation box or inline text
- Color: Green (#22c55e) for GPIO/wiring info
- Font size: Smaller than main labels (8-9pt)

### 4. Expected Voltage Levels Callout

**Location:** In the "Logic notes" box (around line 314-320 in SVG) or create new reference box

**Replace or augment existing "Normal Logic" box with:**
```
✓ Normal Logic (with NEGATIVE mode hopper)

Motor Control (D1/Optocoupler #1):
• GPIO HIGH → LED ON → OUT LOW (< 0.5V) → Motor ON
• GPIO LOW  → LED OFF → OUT HIGH (~6V) → Motor OFF

Input Signals (D7/D5/D6, Optocouplers #2-4):
• Coin:  GPIO LOW = Coin pulse (30ms active LOW)
• Error: GPIO LOW = Error active
• Empty: GPIO LOW = Hopper empty

Voltage Levels (NEGATIVE mode, R1 modified):
• Motor ON:  OUT < 0.5V (LOW threshold per protocol)
• Motor OFF: OUT ~6V (HIGH threshold > 4V per protocol)
  Note: 6V is normal due to voltage divider with hopper input

Reference: docs/azkoyen-hopper-protocol.md sections 2.1 and 6
```

### 5. Optocouplers #2-4 Annotation

**Location:** Near PC817 #2, #3, #4 modules (around lines 173-298 in SVG)

**Add small note:**
```
✓ Optocouplers #2-4: Stock resistors OK
(Input sensing, not drive - no modification needed)
```

**Visual styling:**
- Background: Green (#22c55e)
- Font size: Small (7-8pt)
- Minimal visual weight

### 6. Bill of Materials Update

**Location:** BOM box (around line 305-312 in SVG)

**Update to include:**
```
Bill of Materials

4× PC817 Optocoupler Module
   (bestep, with R1+R2 onboard)
1× 330Ω resistor (1/4W)         ⚠️ NEW
   For R1 modification on Opto #1
1× 2200µF 25V capacitor

⚠️ CH1 (Motor Control) requires
    R1 modification - see notes!
```

---

## Required Annotations for `pinout-diagram-optocoupler.svg`

### 1. D1 Pin Annotation Update

**Location:** D1 pin label and description (around line 94-98 in SVG)

**Current text:**
```
D1
→ Control (Opto #1)
```

**Replace with:**
```
D1 ⚠️
→ Control (Opto #1)
Requires R1 mod!
```

**Add detail box near D1:**
```
D1 (GPIO5) - Motor Control Output
⚠️ Optocoupler #1 R1 modification required:
   Add 330Ω in parallel with stock 1kΩ
   Result: 248Ω → 13.3mA drive current

Without modification: Unreliable motor control
```

**Visual styling:**
- Warning icon (⚠️) next to D1 label
- Detail box: Orange background (#f59e0b)
- Connected with leader line

### 2. Add Critical Configuration Banner

**Location:** Below the main title/subtitle (around line 17 in SVG)

**Add warning banner:**
```
⚠️ CRITICAL: Hopper DIP switch must be in NEGATIVE mode | Optocoupler #1 R1 requires 330Ω parallel resistor
```

**Visual styling:**
- Background: Red gradient (#ef4444 to #dc2626)
- Text color: White
- Full width banner
- Height: ~20px
- Position: y="60" (below subtitle)

### 3. Update Legend

**Location:** Legend box at bottom (around line 130-146 in SVG)

**Add new legend entry:**
```
○ ⚠️ (warning icon) = Modification Required
```

**Visual styling:**
- Warning triangle or exclamation icon
- Color: Orange (#f59e0b)
- Position: After existing legend entries

---

## Color Coding Recommendations

### Highlight Optocoupler #1 as Special

**Current:** Green circle/border for D1 and Control output

**Proposed:** Add orange accent/warning indicator
- Keep green for "active" indication
- Add orange warning icon or border overlay
- Shows "works but needs modification"

### Visual Hierarchy

**Priority 1 (Red):** NEGATIVE mode requirement
**Priority 2 (Orange):** R1 modification requirement
**Priority 3 (Green):** Normal operation indicators
**Priority 4 (Gray):** Unused pins/connections

---

## Implementation Notes

### Tools for Editing

1. **Inkscape** (recommended)
   - Free, open-source SVG editor
   - Full control over text, shapes, and styling
   - Preserves existing SVG structure

2. **Adobe Illustrator**
   - Professional tool
   - May require export/import to maintain compatibility

3. **Online SVG editors**
   - Avoid if possible - may corrupt complex SVG structure

### SVG Editing Best Practices

1. **Before editing:**
   - Create backup copy of original SVG
   - Test opening in multiple browsers (Chrome, Firefox, Safari)

2. **During editing:**
   - Use layers for new annotations (easier to toggle visibility)
   - Maintain existing coordinate system and viewBox
   - Use same font family as existing diagram (Inter, system-ui)
   - Keep consistent stroke widths and styling

3. **After editing:**
   - Validate SVG syntax (W3C validator or Inkscape verification)
   - Test in browser - ensure all text readable at various zoom levels
   - Export optimized SVG (remove unnecessary metadata)

### Text Layout Guidelines

- **Warning boxes:** 200-250px wide max, auto-height
- **Font sizes:**
  - Warning titles: 10pt bold
  - Warning body text: 8-9pt regular
  - Inline annotations: 7-8pt regular
- **Line spacing:** 1.2-1.4 for readability
- **Padding:** 8-12px around text in boxes

---

## Alternative: Add Reference Note in README

**If SVG editing is too complex or time-consuming:**

Add prominent callout in `hardware/README.md` before diagram images:

```markdown
### ⚠️ CRITICAL NOTES FOR WIRING DIAGRAMS BELOW

**Before using these diagrams, read these mandatory requirements:**

1. **Hopper NEGATIVE Mode:** The Azkoyen Hopper U-II DIP switch MUST be set to NEGATIVE mode (active LOW control). See [critical configuration requirements](#-critical-configuration-requirements) above.

2. **Optocoupler #1 R1 Modification:** PC817 motor control optocoupler (channel #1, pin D1) requires resistor modification:
   - Add 330Ω resistor in parallel with stock R1 (1kΩ)
   - Result: 248Ω total → 13.3mA drive current
   - Without this: Motor control will be unreliable or non-functional

3. **Voltage Reference:** Expected voltages with NEGATIVE mode and R1 modification:
   - Motor ON:  OUT < 0.5V (per protocol section 2.1)
   - Motor OFF: OUT ~6V (voltage divider, > 4V threshold OK)

**These requirements are not shown in the diagrams below but are MANDATORY.**

See full details: [Critical Configuration Requirements](#-critical-configuration-requirements)
```

This approach provides the critical information without requiring SVG editing skills.

---

## Verification Checklist

After updating SVG diagrams:

- [ ] NEGATIVE mode requirement visible and prominent (red warning)
- [ ] R1 modification requirement shown on optocoupler #1 (orange warning)
- [ ] Voltage thresholds referenced (< 0.5V, > 4V from protocol doc)
- [ ] Optocouplers #2-4 marked as "stock OK" to avoid confusion
- [ ] D1 pin annotated with modification requirement
- [ ] BOM updated to include 330Ω resistor
- [ ] Reference to `docs/azkoyen-hopper-protocol.md` included
- [ ] SVG renders correctly in browsers (Chrome, Firefox, Safari)
- [ ] Text readable at 100% zoom and 150% zoom
- [ ] No layout breaks or overlapping elements
- [ ] Existing diagram information preserved

---

## Success Criteria

**These updates will be successful if:**

1. A new developer can look at the diagram and immediately see:
   - Hopper must be in NEGATIVE mode (red warning, can't miss it)
   - Optocoupler #1 needs hardware modification (orange callout)
   - Where to find detailed specifications (protocol doc reference)

2. The diagram prevents the 4-hour debugging session by:
   - Making critical requirements impossible to overlook
   - Providing voltage references for verification
   - Linking to authoritative documentation

3. Visual hierarchy guides attention:
   - Red (NEGATIVE mode) = most critical, seen first
   - Orange (R1 mod) = required modification, seen second
   - Green (normal operation) = confirmation/reference

---

## Time Estimate

**SVG editing approach:** 60-90 minutes
- 20-30 min: Set up Inkscape, familiarize with existing SVG structure
- 30-40 min: Add annotations, warning boxes, voltage references
- 10-20 min: Test, verify, optimize, commit

**README note approach:** 5-10 minutes
- Add warning callout before diagram images in hardware/README.md
- Reference critical requirements section
- Commit update

---

**Recommendation:** Start with README note approach (quick, effective), then update SVGs when time permits for professional polish.

---

**Last Updated:** 2026-02-13
**Created By:** Task #7 of hopper-configuration-critical-documentation plan
