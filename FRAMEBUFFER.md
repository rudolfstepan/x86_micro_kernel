# Framebuffer Support

The x86 Microkernel now supports two video modes:

1. **VGA Text Mode** (default) - 80x25 character display
2. **Framebuffer Mode** - 1024x768x32 graphical display with text rendering

## Building

### VGA Text Mode (Default)
```bash
make build-qemu
```

### Framebuffer Mode
```bash
make build-qemu-fb
```

## Running

### VGA Text Mode
```bash
make run
```

### Framebuffer Mode
```bash
make run-fb
```

## Implementation Details

### Compiler Flags
- `USE_VGA_TEXT` - VGA text mode (80x25 characters)
- `USE_FRAMEBUFFER` - Graphical framebuffer mode

### Files Added
- `drivers/video/framebuffer.h` - Framebuffer driver header
- `drivers/video/framebuffer.c` - Framebuffer implementation with 8x16 font
- `drivers/video/display.h` - Display abstraction layer
- `drivers/video/display.c` - Unified display interface

### Multiboot Configuration
The multiboot header (`arch/x86/boot/multiboot.asm`) now requests:
- **VGA mode**: Standard text mode (no special requests)
- **Framebuffer mode**: 1024x768x32 linear framebuffer

### Display Abstraction
Both modes use the same API through `display.h`:
- `display_init()` - Initialize display
- `display_clear()` - Clear screen
- `display_putchar(c)` - Write character
- `display_write(str)` - Write string
- `display_set_color(color)` - Set color
- `display_backspace()` - Handle backspace

### Terminal Dimensions
- **VGA Text**: 80 columns × 25 rows
- **Framebuffer**: 128 columns × 48 rows (calculated from 1024x768 with 8x16 font)

### Font
The framebuffer mode includes a built-in 8x16 bitmap font with:
- Basic ASCII characters (0x20-0x7E)
- Numbers, letters, and common symbols
- Extensible font array for additional glyphs

## Switching Modes

To switch from VGA to framebuffer or vice versa:

1. Clean the build:
   ```bash
   make clean
   ```

2. Build for the desired mode:
   ```bash
   make build-qemu-fb    # For framebuffer
   # or
   make build-qemu       # For VGA text mode
   ```

3. Run with the appropriate target:
   ```bash
   make run-fb           # For framebuffer
   # or
   make run              # For VGA text mode
   ```

## Future Enhancements

Possible improvements:
- [ ] Mouse cursor support in framebuffer mode
- [ ] Additional font sizes (8x8, 16x16)
- [ ] Unicode/UTF-8 support
- [ ] Hardware acceleration via VBE/VESA
- [ ] Multiple resolution options
- [ ] Graphical UI elements (windows, buttons)
- [ ] Image/logo display support

## Notes

- The framebuffer mode requires GRUB to set up the video mode
- QEMU's `-vga std` provides better framebuffer support than `-vga vmware`
- Colors are mapped from VGA palette to 32-bit ARGB for compatibility
- The display abstraction allows code to work in both modes without changes
