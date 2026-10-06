# Toolbox 0.2.0 acceptance record

Validated on Windows 10 x64, Qt 6.11.2, MSVC 19.50 and PowerShell 7.

## Automated validation

- Release configuration and build passed using CLion's bundled CMake and Ninja.
- CTest: TestBase64Codec and TestUi passed (2/2) in both Debug and Release builds.
- Codec: empty, standard, UTF-8, invalid characters, invalid lengths and padding.
- UI: automatic language/theme defaults, overrides, saved preferences, title-bar
  ordering, native topmost flag, grouped navigation, sorting, filtering, lazy
  loading, language refresh, page restoration and Base64 actions.
- Added layout checks at 640x480, 800x600 and 1400x900. The minimum-size test found
  overlapping editors/actions; the Base64 page now scrolls when content cannot fit.
- Closing windows during group animations is covered by the regression workflow.
- Navigation responsiveness regression: 12 rapid mouse clicks toggle the selected
  Base64 group's visible rows immediately, without changing page, saved selection,
  factory count or editor contents. Arrows/fades are cancellable 140ms effects;
  parent/child markers update immediately. Group headings and indent guides now
  make the hierarchy explicit. Perceived smoothness remains a manual check.

Tests use temporary settings and native windows hidden from the desktop. They do
not replace a complete interactive acceptance session.

## Actual window capture

The user's local `wgccli` successfully captured the running Toolbox Settings page.
The inspected capture confirms the requested title-bar positions, collapsible
Text and Encoding group, Chinese labels and a single language setting.
Captures are under `out/acceptance/` and excluded from source control.

```powershell
wgccli --title Toolbox --exact --require-unique --out out/acceptance/toolbox.png --json
```

The desktop capture needs the user's interactive session. The restricted command
environment returned no windows. The computer-use helper's capture timed out;
its indexed click also failed with `coordinate input geometry is unavailable`.
The CLion UI has now been inspected with wgccli: the project opens, CMake
configuration/generation completes, and Toolbox/TestUi/TestBase64Codec appear in
the target list. Selecting Toolbox in the IDE Run menu successfully starts the
Debug executable. The Run panel displays the correct executable path and a
wgccli capture confirms its running native window. Interactive mouse checks
remain pending; both IDE startup and its command-line CMake toolchain are verified.

## Distribution

`scripts/Package-Windows.ps1` builds/tests Release, deploys Qt dependencies and the
MSVC runtime installer, and creates a ZIP plus SHA256 checksum in `dist/`.
Keep the DLL/plugin folders alongside Toolbox.exe. Install the included MSVC
runtime if the destination machine lacks it. Clean-machine validation and a
signed installer are not part of the current validation.

The final ZIP was extracted to `out/acceptance/verified-package/` and its Release
executable launched successfully. Process-module inspection confirms Qt6Core,
Qt6Gui and Qt6Widgets load from the extracted directory. A wgccli capture confirms
the running Release window. This smoke test uses the development machine and
does not prove that a clean machine has the required MSVC runtime.

## Follow-up

- Complete interactive checks: minimize/maximize, dragging/resizing, group and
  compact-sidebar clicks, copy-to-clipboard and keyboard navigation.
- Test the archive on a clean Windows machine before publishing a release.
- Next feature stage: persistent navigation configuration, search and recent tools.
