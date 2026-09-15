# Toolbox

A desktop toolbox application built with Qt 6 and C++20.

## Features

- Single-window tool management with sidebar navigation
- Lazy-loaded tool pages
- Extensible tool registration system
- Dark theme UI

## Current Tools

- **Base64** — Encode/decode text to/from Base64

## Building

### Prerequisites

- Qt 6.x (MSVC 2022 64-bit)
- CMake 3.24+
- MSVC 2022 Build Tools

### Build Steps

```powershell
cmake --preset default
cmake --build cmake-build-debug
```

To deploy Qt DLLs:

```powershell
windeployqt cmake-build-debug\app\Toolbox.exe
```

## Architecture

```
app/          — Application entry point, MainWindow
core/         — Tool framework, navigation, services
  tool/       — ToolDescriptor, ToolPage, ToolRegistry, ToolPageManager
  navigation/ — NavigationGroup, NavigationItem, NavigationManager
  services/   — AppServices, SettingsService
ui/           — Sidebar, Theme
tools/        — Individual tool implementations
  encoding/
    base64/   — Base64 encoder/decoder
tests/        — Unit tests
```

## Credits

The dark theme color scheme is inspired by [QWidget-FancyUI](https://github.com/COLORREF/QWidget-FancyUI) by [COLORREF](https://github.com/COLORREF), licensed under [GPL-3.0](https://github.com/COLORREF/QWidget-FancyUI/blob/main/LICENSE).

Specifically, the "Default Dark" color profile (`#1A2034` base) from their `ColorProfile.json` was used as reference for the theme colors defined in `ui/Theme.h`.

QWidget-FancyUI is a Qt 6 UI component library with animations, custom themes, and window styles. This project does not include any source code from QWidget-FancyUI — only the color values were used as design reference.

## License

This project is licensed under the MIT License.