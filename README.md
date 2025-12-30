# Advanced Commodore Editor (C64 Ultimate) — v2.2

**A Nano-style full-screen text editor for the Commodore 64 Ultimate**  
Author: **Dr. Eric O. Flores**  
Copyright © 2025  
Language / Toolchain: **C (cc65 / cl65)**  
Target Platform: **Commodore 64 / C64 Ultimate**

---

## What this program is

**Advanced Commodore Editor (C64 Ultimate)** is a native, full-screen text editor designed specifically for the **Commodore 64**, with special attention given to the **C64 Ultimate** and its USB-based storage workflow.

The editor is inspired by the philosophy of the **Linux `nano` editor**:  
simple controls, fast interaction, minimal cognitive overhead, and clear on-screen prompts — while respecting the technical constraints and historical behavior of vintage hardware.

This is **not an emulator-side tool**.  
It is a **real Commodore 64 program**, compiled with `cc65`, intended to run directly on original or FPGA-based C64 systems.

---

## What it can do

### Core Editing Features
- Full-screen text editing on a **40×25 C64 display**
- Cursor navigation using standard **C64 cursor keys**
- Insert text, backspace, and create new lines
- **Hard wrap at 40 columns** while typing (comfortable on a 40-column screen)
- Internal line capacity of **up to 80 characters** to preserve longer lines in files

### Status Bar & Viewport
- Clear screen layout:
  - **Top line**: menu bar
  - **Middle area**: text viewport
  - **Bottom line**: status line and prompts
- Status line shows:
  - Active IEC device number
  - Current filename (or *Untitled*)
  - Typing mode (UPPER / mixed)
  - Line and column position

### Menus (Improved Usability)
- **F1** → File Menu  
  Open, Save, Save As, Close, Quit
- **F3** → Edit Menu  
  Copy Line, Cut Line, Paste Line, Search, Search & Replace
- **F5** → Help Menu  
  About screen

Menus are displayed as **boxed overlays**, replacing earlier single-line menu designs that were harder to use.

### Clipboard (Line-Based)
- Copy current line
- Cut current line
- Paste line below the cursor

### Search Tools
- Forward search from cursor position
- Search and replace (single replacement at current match)

### Typing Mode / Capital Letters Fix
- **CTRL+U** toggles typing mode:
  - **UPPER** (default): forces uppercase letters
  - **mixed**: allows mixed-case typing where supported

### File I/O (C64 Ultimate Friendly)
- Open and Save via **IEC device numbers**
- Default device is **8**, but configurable at runtime
- Designed for **USB storage mapped by C64 Ultimate**
- Uses CBM-style sequential file access

---

## How to build on Linux (Pop!_OS)

```bash
sudo apt update
sudo apt install cc65
cl65 -t c64 -O -o adveditC64u.prg adveditC64u.c
```

---

## How to run on a Commodore 64 / C64 Ultimate

```basic
LOAD"ADVEDITC64U.PRG",8,1
RUN
```

---

## Why “25×80”?

The **Commodore 64 screen is always 40×25 characters**.

This editor uses:
- **40 columns** for display
- **80 columns internally per line**

This allows comfortable editing on real hardware while preserving longer lines in files.

---

## Version History Highlights

- **Linux Base Frame Editor**  
  Minimal C editor on Pop!_OS with a basic File menu only.

- **Version 2.x (C64 Port)**  
  Native cc65 rewrite for Commodore 64.

- **Version 2.2 (Current)**  
  Visible cursor, 40-column wrap, uppercase typing toggle, boxed menus, stable USB/IEC I/O.

---

## Origin and Project Background

This project began as a **Linux C base frame editor** and was later **ported and expanded** to become a native Commodore 64 editor optimized for the **C64 Ultimate**.

---

## Donation (Optional)

If you find this project helpful and wish to support the author, you may optionally buy me a beer via PayPal:

**PayPal:** `@floreseo`  
*(Completely optional — education comes first.)*

---

## License (MIT – Summary)

This project is released under the **MIT License**.  
The software is provided **“as is”**, without warranty of any kind.

---

## Legal Notice and Disclaimer

### No Warranty

This guide and software are provided **“as is,” without warranty of any kind**, express or implied.

The author assumes **no responsibility or liability** for outcomes including data loss, hardware damage, disk corruption, or user error.

### Educational Use Only

This material is intended **solely for educational, instructional, and historical purposes**.  
It is not official documentation and is not affiliated with any company.

### Trademarks

All trademarks and product names belong to their respective owners and are used for identification and historical reference only.

- Commodore 64 — Commodore Business Machines  
- ABACUS — Abacus Super-C is a C programming language compiler designed for the Commodore 64 and Commodore 128 computers, developed by Thomas Eirich and Franz J. Hauck  
- 1541 / 1571 Drives — Commodore  
- SD2IEC — respective developers
