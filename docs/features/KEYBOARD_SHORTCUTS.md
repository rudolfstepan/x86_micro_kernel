# Keyboard Shortcuts - Quick Reference Card

**x86 Microkernel Shell - User Guide**

---

## Command History

| Key | Action | Description |
|-----|--------|-------------|
| **↑** (Up Arrow) | Previous Command | Navigate backward through command history |
| **↓** (Down Arrow) | Next Command | Navigate forward through command history |
| `history` | List History | Display all commands in history buffer (50 entries) |

**Example**:
```bash
> ls
> cd /
> mem
> ↑        # Shows: mem
> ↑        # Shows: cd /
> ↑        # Shows: ls
> ↓        # Shows: cd /
> history  # Lists all commands
```

---

## Line Editing

| Key | ASCII | Action | Description |
|-----|-------|--------|-------------|
| **Backspace** | `0x08` | Delete Left | Delete character to the left of cursor |
| **Delete** | `ESC[3~` | Delete Right | Delete character at cursor position |
| **Home** | `ESC[H` | Jump to Start | Move cursor to beginning of line |
| **End** | `ESC[F` | Jump to End | Move cursor to end of line |

---

## Control Shortcuts

| Shortcut | ASCII | Action | Description |
|----------|-------|--------|-------------|
| **Ctrl+C** | `0x03` | Cancel | Clear current line, show `^C`, reset history |
| **Ctrl+D** | `0x04` | EOF | Exit shell (when line is empty) |
| **Ctrl+L** | `0x0C` | Clear Screen | Clear entire VGA display, redisplay prompt |
| **Ctrl+U** | `0x15` | Clear Line | Erase entire current line |
| **Ctrl+K** | `0x0B` | Kill to End | Delete from cursor to end of line |

**Tips**:
- **Ctrl+C**: Great for canceling mistyped commands without executing
- **Ctrl+L**: Quick way to clean up cluttered screen
- **Ctrl+U**: Fast way to start over when typing long command
- **Ctrl+K**: Remove everything after cursor (useful for editing)

---

## Shell Commands

### File System
| Command | Arguments | Description |
|---------|-----------|-------------|
| `ls` | `[path]` | List files in directory (default: current) |
| `cd` | `<path>` | Change current directory |
| `cat` | `<file>` | Display contents of text file |
| `run` | `<file.prg>` | Execute user program (.PRG format) |

### System Information
| Command | Arguments | Description |
|---------|-----------|-------------|
| `mem` | - | Display memory usage (heap, free, allocated) |
| `dump` | `<addr> <end>` | Hex dump memory region |
| `lspci` | - | List PCI devices on system bus |
| `arp` | - | Display ARP table (network layer 2) |

### Shell Control
| Command | Arguments | Description |
|---------|-----------|-------------|
| `help` | - | Show all available commands and shortcuts |
| `history` | - | Display command history (last 50 commands) |
| `clear` | - | Clear screen (same as Ctrl+L) |
| `exit` | - | Exit shell |

---

## Special Keys (Detected but Not Yet Implemented)

| Key | ESC Sequence | Status | Planned Use |
|-----|-------------|--------|-------------|
| **← Left Arrow** | `ESC[D` | ⏳ Priority 3 | Move cursor left within line |
| **→ Right Arrow** | `ESC[C` | ⏳ Priority 3 | Move cursor right within line |
| **F1-F12** | `ESC[11~` etc. | ⏳ Priority 3 | Function key shortcuts |
| **Page Up** | `ESC[5~` | ⏳ Priority 3 | Scroll up (future feature) |
| **Page Down** | `ESC[6~` | ⏳ Priority 3 | Scroll down (future feature) |
| **Tab** | `0x09` | ⏳ Priority 3 | Command/filename autocomplete |

---

## Modifier Keys

| Key | Status | Description |
|-----|--------|-------------|
| **Left Shift** | ✅ Working | Shift key for uppercase/symbols |
| **Right Shift** | ✅ Working | Independent right shift detection |
| **Left Ctrl** | ✅ Working | Control key for shortcuts (Ctrl+C, etc.) |
| **Right Ctrl** | ✅ Working | Independent right control detection |
| **Left Alt** | ✅ Working | Alt key detection (not yet used in shell) |
| **Right Alt** | ✅ Working | AltGr key for international layouts |
| **Caps Lock** | ✅ Working | Toggle capslock state (LED not yet impl.) |

**API Functions** (for programmers):
```c
bool kb_is_shift_pressed(void);  // Check if either Shift is down
bool kb_is_ctrl_pressed(void);   // Check if either Ctrl is down
bool kb_is_alt_pressed(void);    // Check if either Alt is down
```

---

## Command History Behavior

### Duplicate Prevention
- If you type the same command twice in a row, it's only added to history **once**
- Example:
  ```bash
  > ls
  > ls       # NOT added to history (duplicate)
  > cd /
  > ls       # Added to history (not consecutive duplicate)
  ```

### Empty Command Filtering
- Blank lines (just pressing Enter) are **not added** to history
- This keeps your history clean and useful

### Circular Buffer (50 Entries)
- History holds up to **50 commands**
- When full, oldest commands are automatically overwritten
- Most recent 50 commands always available

### Navigation Reset
- Pressing **Ctrl+C** resets history navigation to newest entry
- This prevents confusion after canceling a line

---

## Tips & Tricks

### Quick Clear
Instead of typing `clear`, just press **Ctrl+L** (faster!)

### Fix Typos Fast
If you mistype at the end of a long command:
1. Press **Ctrl+U** to clear entire line, or
2. Use **Backspace** to delete last characters

### Cancel Long Command
If you start typing a command and change your mind:
- Press **Ctrl+C** to cancel without executing

### Reuse Recent Commands
Don't retype long commands:
1. Press **Up Arrow** to find previous command
2. Press **Enter** to execute it again
3. Or edit it first, then press **Enter**

### View What You've Done
Type `history` to see all your recent commands. Great for:
- Remembering what you did earlier
- Finding that long command you typed before
- Debugging what caused an error

---

## Keyboard Layout

The shell currently supports **US QWERTY** layout. Keys are mapped as follows:

### Normal (Unshifted)
```
` 1 2 3 4 5 6 7 8 9 0 - =
  q w e r t y u i o p [ ]
  a s d f g h j k l ; '
  z x c v b n m , . /
```

### Shifted
```
~ ! @ # $ % ^ & * ( ) _ +
  Q W E R T Y U I O P { }
  A S D F G H J K L : "
  Z X C V B N M < > ?
```

**Note**: International layouts (UK, DE, FR, etc.) not yet implemented. See Priority 3 for future plans.

---

## Testing Your Keyboard

### Check Basic Keys
Type this command and press Enter:
```bash
> echo 1234567890 abcdefghijklmnopqrstuvwxyz
```

### Check Shift Keys
Type uppercase letters and symbols:
```bash
> echo ABCDEFGHIJKLMNOPQRSTUVWXYZ !@#$%^&*()_+
```

### Check Ctrl Keys
Try each Ctrl shortcut:
- Type some text, then press **Ctrl+C** (should show `^C`)
- Press **Ctrl+L** (should clear screen)
- Type text, press **Ctrl+U** (should clear line)

### Check History
Type several commands, then test arrow keys:
```bash
> ls
> cd /
> mem
> ↑        # Should show: mem
> ↑        # Should show: cd /
> ↓        # Should show: mem
> history  # Should list all three commands
```

---

## Troubleshooting

### Arrow Keys Not Working
**Symptom**: Pressing Up/Down shows `^[[A` or `^[[B` instead of navigating history  
**Cause**: ESC sequence not being parsed correctly  
**Solution**: This shouldn't happen with Priority 1+2 implementation. Report bug.

### Ctrl+Key Not Working
**Symptom**: Pressing Ctrl+C types garbage character instead of canceling  
**Cause**: Keyboard driver not detecting Ctrl key properly  
**Solution**: Check if `kb_is_ctrl_pressed()` returns true when Ctrl is held.

### History Not Remembering Commands
**Symptom**: Up arrow doesn't show previous commands  
**Cause**: Commands might not be added to history (empty lines, duplicates filtered)  
**Solution**: Check `history` command output. If empty, report bug.

### Screen Glitches When Editing
**Symptom**: Text overwrites incorrectly, cursor jumps around  
**Cause**: VGA functions may have bugs  
**Solution**: Press **Ctrl+L** to clear and start fresh. Report visual glitches.

---

## For Programmers: API Integration

If you're writing user programs (`.PRG` files), you can use these syscalls:

### Keyboard Input
```c
#include <lib/libc/stdio.h>

char c = getchar();           // Read single character (blocks)
char *line = gets(buffer);    // Read entire line (with line editing)
```

### Screen Output
```c
#include <lib/libc/stdio.h>

putchar('A');                 // Write single character
puts("Hello, World!");        // Write string with newline
printf("Value: %d\n", 42);    // Formatted output
```

### Keyboard State Query
```c
#include "drivers/char/kb.h"

if (kb_is_ctrl_pressed()) {
    // Ctrl key is currently held down
}

if (kb_is_shift_pressed()) {
    // Shift key is currently held down
}

if (kb_is_alt_pressed()) {
    // Alt key is currently held down
}
```

---

## Version History

**Priority 1 (Keyboard Driver)** - Extended Scancode Support
- ✅ Arrow keys, function keys working
- ✅ Ctrl/Alt detection
- ✅ Thread-safe input queue
- ✅ Atomic operations (race condition fixes)

**Priority 2 (Shell Enhancements)** - Line Editing & History
- ✅ 50-entry command history with duplicate prevention
- ✅ Up/Down arrow navigation through history
- ✅ Ctrl+C/L/U/K shortcuts for editing
- ✅ VGA line manipulation functions
- ✅ Enhanced help with keyboard shortcuts
- ✅ `history` command

**Priority 3 (Planned)** - Advanced Features
- ⏳ Left/right arrow cursor positioning
- ⏳ Tab autocomplete (commands and filenames)
- ⏳ Raw vs cooked terminal modes
- ⏳ Configurable echo (password input)
- ⏳ LED control (Caps Lock indicator)
- ⏳ Multiple keyboard layouts (UK, DE, FR)

---

## Quick Reference Card (Print This!)

```
╔══════════════════════════════════════════════════════════════╗
║            X86 MICROKERNEL - KEYBOARD SHORTCUTS              ║
╠══════════════════════════════════════════════════════════════╣
║ HISTORY:          ↑/↓  Navigate  |  history  List all       ║
║ EDITING:   Backspace  Delete Left |  Delete  Delete Right   ║
║ CTRL+C:          Cancel current line (show ^C)              ║
║ CTRL+L:          Clear screen, redisplay prompt             ║
║ CTRL+U:          Clear entire current line                  ║
║ CTRL+K:          Kill to end of line                        ║
║ CTRL+D:          Exit shell (EOF)                           ║
╠══════════════════════════════════════════════════════════════╣
║ COMMANDS:  help  All commands  |  clear  Clear screen       ║
║            ls    List files    |  cd     Change dir         ║
║            cat   Show file     |  run    Execute .PRG       ║
║            mem   Memory info   |  lspci  PCI devices        ║
╚══════════════════════════════════════════════════════════════╝
```

---

**For detailed technical documentation, see**:
- `KEYBOARD_ANALYSIS.md` - Comparison to Linux/MS-DOS
- `KEYBOARD_IMPROVEMENTS.md` - Priority 1 technical details
- `SHELL_ENHANCEMENTS.md` - Priority 2 implementation guide
