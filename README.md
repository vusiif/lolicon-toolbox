# Toolbox

A Qt 6 / C++20 desktop toolbox using ElaWidgetTools for its window, title bar,
navigation, theme and controls.

## Features

- One window with collapsible tool categories, lazy-loaded pages and a Settings footer
- Base64 lives under Text and Encoding; navigation configuration controls group/tool order and visibility
- Title bar: menu, app icon/title, theme, pin, minimize, maximize, close
- Light/dark theme follows the system by default; the title bar provides a manual override
- Automatically detects system UI language (English / simplified or traditional Chinese);
  Settings allows following the system or choosing an explicit language
- Persisted theme, language, window geometry, navigation state and last page
- Base64 encoding/decoding with validation, clear and clipboard actions
- Local Mojibake Repair with ranked candidates, reversible paths, background
  analysis, cancellation and a maximum input size of 1 MiB

## Build

Requirements: Windows, Qt 6.11.2 MSVC 2022 64-bit including Widgets private headers,
MSVC Build Tools, CMake and Ninja. Initialize the MSVC x64 environment before
running CMake from PowerShell 7 (`pwsh`). The preset contains the local Qt path.

```powershell
cmake --preset default
cmake --build cmake-build-debug
windeployqt cmake-build-debug/app/Toolbox.exe
ctest --test-dir cmake-build-debug --output-on-failure
```

For a Release build, use the `release` configure/build/test presets. To build,
test and create a fresh Windows x64 ZIP containing Qt and MSVC runtime files,
run the following in PowerShell 7 after initializing the MSVC x64 environment:

```powershell
./scripts/Package-Windows.ps1 -CMakePath cmake
```

The output lives in `dist/`, with a SHA256 checksum beside the ZIP. Extract the
archive and run `Toolbox.exe` in its folder. No Qt installation is needed on the
target machine. The MSVC x64 runtime is required; if it is missing, install the
included `vc_redist.x64.exe` before running Toolbox. This is a portable development
package, not an installer.

In CLion, open the project root and select the `default` or `release` CMake preset
with an MSVC x64 toolchain. Build/run the `Toolbox` target; the two test targets
are also available. The preset's Qt path is local and can be overridden.

## Structure

```text
app/             Application lifetime and ElaWindow integration
core/tool/       Tool descriptors, registry and page lifecycle
core/navigation/ Group/item configuration, validation and ordering
core/services/   AppSettings: validated settings and translation installation
ui/              LazyToolPage, SettingsPage and small Ela palette helpers
tools/encoding/  Base64 page and unchanged Base64Codec
translations/    Chinese translation sources
tests/           Codec tests and native Windows UI regression tests
third_party/     base64.hpp and vendored ElaWidgetTools
```

Ela owns navigation selection and the page stack. Toolbox does not maintain a
second stack or duplicate sidebar. Lazy wrappers create each tool page once;
language changes update existing controls. Settings changes update one store,
and the title bar uses the same ElaTheme instance as the pages. System changes
update automatic preferences without saving them as manual overrides.

Register a tool in `Application.cpp`, then add its stable ID to a navigation group.
`NavigationManager` keeps placement separate from the tool registry: groups and
items sort by `order` with stable IDs breaking ties. Hidden/unregistered tools and
empty groups are omitted. The window renders these sections using Ela expanders;
group titles refresh when the language changes. The current defaults live in code;
user-editable groups and JSON persistence are future work.

The UI tests use temporary settings files and hidden native windows. They verify
system defaults and overrides, button placement, the native topmost flag, theme synchronization, lazy loading,
page switching, three-language refresh, Base64 actions and restart restoration.
Rendered previews are saved as `cmake-build-debug/tests/rebuilt-*.png`.

## Third-party software

[ElaWidgetTools](https://github.com/Liniyous/ElaWidgetTools) is linked statically
from `third_party/ElaWidgetTools-main/ElaWidgetTools`; its example is not built.
MIT license, copyright (c) 2024 Liniyous; the original license is retained in
`third_party/ElaWidgetTools-main/LICENSE`.

Local Ela adaptation: the navigation view owns its style, and navigation animations
are parented to that style, preventing callbacks into a destroyed viewport when a
window closes during a group animation.
Group visibility and selected markers update immediately. Arrow/fade effects use
one cancellable 140ms animation each; mouse clicks bypass kinetic scrolling.
Child rows use a 32px indent and guide lines, while group titles are bold.

Ela uses Qt Widgets private APIs: deploy the same Qt version used for the build,
and rebuild Ela and Toolbox together after upgrading Qt.

The Base64 codec uses the vendored `third_party/base64.hpp` implementation.

## Mojibake Repair

Open **Text and Encoding → Mojibake Repair**, paste the original garbled text,
and choose **Analyze**. Compare up to five candidates, inspect **Recovery process**
and copy the selected result. Editing the input invalidates old results; typing
does not automatically run analysis. All work stays local, with no AI/API calls.

The tested Qt 6.11.2 kit provides UTF-8, Latin-1, Windows-1252, GBK, GB18030,
Big5, Shift-JIS and EUC-KR via named QStringEncoder/QStringDecoder instances.
Every conversion checks exact byte/text round-trips. Other Qt kits may provide
fewer encodings; unavailable codecs are omitted at runtime.

Search is limited to two conversions, 100 unique candidates and a 12-state beam.
Scores are heuristics rather than calibrated probabilities. Plausible normal
text stays the preferred result; close candidates and lost information remain
uncertain. Mixed Western garbling can be repaired in representable segments,
but complete-text reversibility is explicitly marked unproven for such paths.
Files, directory scans and AI fallback are outside this first tool version.

Regression coverage includes verified byte fixtures, Chinese/Japanese/Big5/Korean
cases, double mojibake, original preservation, invalid/lossy input, inverse paths,
size/performance bounds and asynchronous page lifecycle. `TestMojibake` results
are saved in the build's `tests/mojibake-results.txt`.

MSVC builds use a small compiler launcher to emit UTF-8 diagnostics and detect
the actual `/showIncludes` prefix. This avoids stale objects caused by localized
include output being parsed with the wrong codepage.

## License

MIT.
