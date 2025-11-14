# CLAUDE.md - lvgl_micropython

This file provides guidance for working with the lvgl_micropython build system and repository structure.

## Project Overview

**lvgl_micropython** is a binding project that brings LVGL (Light and Versatile Graphics Library) to MicroPython. It's a fork/spinoff of the official lv_micropython and lv_binding_micropython projects, designed to:
- Simplify compilation
- Provide a common API for adding drivers
- Support more display and input device connection topologies
- Improve build system flexibility

This is a **composite repository** containing multiple large subprojects as git submodules.

## Critical Build Rules

⚠️ **IMPORTANT - READ FIRST**:

1. **DO NOT initialize submodules manually** - The build system handles this automatically
2. **DO NOT use** information from official LVGL bindings - this is a custom fork
3. **Clone and build process**:
   ```bash
   git clone https://github.com/lvgl-micropython/lvgl_micropython
   cd lvgl_micropython
   python3 make.py esp32 ...
   ```
4. **To update to latest**: Delete local copy and re-clone from scratch (don't use git pull)
5. **Windows is NOT supported** - Build on Linux/macOS only

## Repository Structure

### Top-Level Directories

- **`lib/`**: Git submodules for major dependencies
  - `micropython/`: MicroPython interpreter (the Python runtime)
  - `lvgl/`: LVGL graphics library (C99 framework)
  - `esp-idf/`: Espressif IoT Development Framework (ESP32 SDK)
  - `SDL/`: Simple DirectMedia Layer (for desktop builds)
  - `pycparser/`: Python C parser (used by code generator)

- **`builder/`**: Build system scripts (per-platform build logic)
  - `__init__.py`: Core build orchestration
  - `esp32.py`: ESP32-specific build configuration
  - `unix.py`: Unix/Linux build configuration
  - `macOS.py`: macOS build configuration
  - `stm32.py`, `rp2.py`, `nrf.py`, etc.: Other platform configs
  - `toml_reader.py`: TOML configuration file parser

- **`api_drivers/`**: Display and touch controller drivers
  - `common_api_drivers/`: C-based drivers (display ICs, touch ICs)
  - `py_api_drivers/`: Python-based driver wrappers

- **`ext_mod/`**: LVGL MicroPython binding code (the glue between Python and C)

- **`gen/`**: Generated binding code (auto-generated from LVGL headers)

- **`micropy_updates/`**: Patches and updates to MicroPython core

- **`build/`**: Build output directory (created during compilation)
  - Contains compiled binaries
  - Contains intermediate build artifacts
  - `display.py`: Generated display driver configuration (from TOML)

- **`custom_board_and_toml_examples/`**: Example configurations for custom boards

- **`display_configs/`**: Predefined display configuration files

## Build System - make.py

The primary entry point is **`make.py`**, a Python script that orchestrates the entire build process.

### Basic Usage

```bash
python3 make.py <target> [options]
```

**Supported targets**:
- `esp32`: ESP32 microcontrollers
- `unix`: Linux desktop
- `macOS`: macOS desktop
- `stm32`: STM32 microcontrollers
- `rp2`: Raspberry Pi Pico (RP2040)
- `nrf`: Nordic nRF series
- `renesas-ra`: Renesas RA series
- `mimxrt`: NXP i.MX RT series
- `samd`: Microchip SAMD series
- `raspberry_pi`: Raspberry Pi (full Linux)

### Command-Line Arguments

**Custom board support**:
```bash
--custom-board-path <path>  # Path to custom board definition directory
```

**TOML configuration**:
```bash
--toml <path>  # Path to TOML configuration file for build customization
```

**Build control**:
```bash
-clean  # Clean build artifacts before building
```

**LVGL compiler flags**:
```bash
-LV_CFLAGS <flags>  # Additional compiler flags for LVGL
```

### Build Flow

1. **Parse arguments**: Process target, custom board path, TOML config
2. **Load TOML** (if provided): Parse board configuration, generate display.py
3. **Select builder**: Import appropriate platform builder (esp32.py, unix.py, etc.)
4. **Setup environment**: Configure paths, dependencies, compiler flags
5. **Fetch submodules**: Automatically clone/update required submodules (LVGL, MicroPython, etc.)
6. **Apply patches**: Apply MicroPython updates from `micropy_updates/`
7. **Generate bindings**: Run code generator to create Python↔C glue code
8. **Compile**: Build MicroPython with LVGL binding and drivers
9. **Output**: Place binaries in `build/` directory

### TOML Configuration System

TOML files provide declarative board configuration instead of command-line flags.

**Example TOML structure**:
```toml
[board]
name = "Custom ESP32 Board"
mcu = "esp32s3"

[display]
driver = "st7789"
width = 320
height = 240
bus = "spi"

[touch]
driver = "cst816s"
bus = "i2c"
```

The `toml_reader.py` module:
- Parses TOML configuration
- Generates `build/display.py` driver initialization code
- Converts TOML settings into make.py command-line arguments
- Simplifies custom board definitions

## How MicroPythonOS Uses This

MicroPythonOS uses **`scripts/build_mpos.sh`** which wraps `make.py`:

```bash
# From MicroPythonOS root:
./scripts/build_mpos.sh unix dev
./scripts/build_mpos.sh esp32 prod waveshare-esp32-s3-touch-lcd-2
```

The build script:
1. Calls `lvgl_micropython/make.py` with appropriate flags
2. Passes display driver configuration (e.g., st7789, cst816s)
3. Adds MicroPythonOS-specific C modules (camera, secp256k1, quirc)
4. Creates symlinks to MicroPythonOS manifests
5. Handles ESP32 board variants (waveshare, fri3d-2024, etc.)

## Display and Touch Drivers

### Supported Display ICs (common_api_drivers/display/)
- **st7789**: 240x320 TFT LCD (used by MicroPythonOS)
- **ili9341**: 240x320 TFT LCD
- **gc9a01**: 240x240 round TFT LCD
- **ssd1306**: OLED displays
- And many more...

### Supported Touch ICs (common_api_drivers/indev/)
- **cst816s**: Capacitive touch controller (used by MicroPythonOS)
- **ft6x36**: Capacitive touch controller
- **xpt2046**: Resistive touch controller
- And more...

### Driver API

Drivers expose a Python API through `py_api_drivers/` wrappers:
```python
import lvgl as lv
from display_driver import DisplayDriver

# Initialize display
display = DisplayDriver()

# Use LVGL
screen = lv.obj()
label = lv.label(screen)
label.set_text("Hello LVGL!")
```

## Special Features

### RGB Bus Driver Optimization

The RGB bus driver uses a special dual-core optimization:
- Two **full frame buffers** hidden in C code (in SPIRAM)
- User allocates smaller **partial buffers** (e.g., 1/10 display size)
- Second ESP32 core handles buffer copying in background
- LVGL renders to partial buffer while copying happens on other core
- Screen rotation handled in C for better performance
- Reduces memory pressure while maintaining smooth rendering

### Code Generation (gen/)

The binding between Python and LVGL C API is **auto-generated**:
- Parses LVGL headers using pycparser
- Generates Python wrapper functions
- Creates type conversions between Python objects and C structs
- Handles memory management and garbage collection

## Common Tasks

### Adding a New Display Driver

1. Create driver in `api_drivers/common_api_drivers/display/<driver_name>/`
2. Implement standard driver API (init, flush, etc.)
3. Create Python wrapper in `api_drivers/py_api_drivers/`
4. Add to build system in appropriate `builder/<target>.py`
5. Test with: `python3 make.py unix --display <driver_name>`

### Custom Board Definition

1. Create directory: `custom_board_and_toml_examples/my_board/`
2. Create TOML: `my_board/board_config.toml`
3. Build: `python3 make.py esp32 --custom-board-path custom_board_and_toml_examples/my_board/`

### Debugging Build Issues

- **Clean build**: `python3 make.py <target> -clean`
- **Verbose output**: Check builder script output for errors
- **Check submodules**: Ensure `lib/micropython/`, `lib/lvgl/` exist
- **Build directory**: Inspect `build/` for generated files and logs
- **Environment**: Verify CMake, Ninja, compilers are installed

## Integration with MicroPythonOS

When building MicroPythonOS, the integration works as follows:

1. **MicroPythonOS manifests** are symlinked into `lib/micropython/ports/<target>/boards/`
2. **C modules** (camera, secp256k1, c_mpos) are symlinked as user modules
3. **Display config** is passed via command-line flags or TOML
4. **Frozen filesystem** (if prod build) is included via manifest.py
5. **Output binary** goes to `lvgl_micropython/build/lvgl_micropy_<target>`
6. **MicroPythonOS** then uses this binary via `scripts/run_desktop.sh`

## Key Files to Understand

- **`make.py`**: Main build orchestrator (start here)
- **`builder/__init__.py`**: Core build logic
- **`builder/esp32.py`**: ESP32 platform specifics (board config, IDF integration)
- **`builder/unix.py`**: Desktop platform specifics (SDL integration)
- **`lib/lv_conf.h`**: LVGL configuration (features, memory, rendering)
- **`ext_mod/`**: Python binding implementation

## Version Information

The binding tracks three separate version streams:
- **LVGL version**: Currently 9.3.0 (in lib/lvgl/)
- **MicroPython version**: Tracks upstream (in lib/micropython/)
- **Binding version**: Independent versioning for the glue code

Updates to any component require careful synchronization and testing.

## Important Notes

- **Submodules**: Never manually run `git submodule update` - let make.py handle it
- **Platform limitations**: Not all displays/touch drivers work on all platforms
- **Memory**: ESP32 builds require SPIRAM for LVGL framebuffers
- **Threading**: MicroPython uses single core; second core used for display DMA
- **Build time**: First build downloads submodules and compiles everything (~10-30 minutes)
- **Incremental builds**: Subsequent builds are faster (use -clean to force full rebuild)

## Getting Help

- **README.md**: Detailed driver lists and build instructions
- **GitHub Issues**: https://github.com/lvgl-micropython/lvgl_micropython/issues
- **MicroPythonOS docs**: https://docs.micropythonos.com for integration specifics
