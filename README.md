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
- Local Text Style Converter: Classic Martian, three intensities, paired line
  decorations, reproducible numeric or case-insensitive ASCII alphanumeric seeds
  with a compact seed row. Convert uses the displayed seed; Random convert changes
  the seed and converts in one click; the dice only changes the seed.
  The attributed merged pack contains 4,791 character and 38 phrase rules
  (614 with alternatives), combining three reference dictionaries and the original
  curation. Data license status is mixed; see the source records and packaged notices.
  Unknown Unicode graphemes remain intact; only line endings are normalized.
  Input is limited to 256 KiB UTF-8 and output to 2 MiB; tasks run in the background
  with cancellation. Text and seeds are not persisted.

Text Style Converter documentation: [contract](docs/Text-Style-Converter.md),
[manual testing](docs/Text-Style-Testing.md), [data sources](docs/Style-Data-Sources.md).
Core and hidden native UI regressions are automated; real clipboard acceptance
for the new page remains manual because SogouCloud held the clipboard during testing.

## Build

Requirements: Windows, Qt 6.11.2 MSVC 2022 64-bit including Widgets private headers,
MSVC Build Tools, CMake, Ninja and Python 3 (build-time JSON validation only). Initialize the MSVC x64 environment before
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

Packaging keeps the newest three local builds. Older ZIPs and checksum files are
uploaded to the GitHub `dist-archive` prerelease; local directories/ZIPs/checksums
are removed only after remote size and SHA-256 verification. GitHub CLI authentication
is required; an upload failure preserves the affected local build. Inspect or run
retention separately in PowerShell 7:

```powershell
./scripts/Archive-Old-Dist.ps1 -PlanOnly
./scripts/Archive-Old-Dist.ps1
```

In CLion, open the project root and select the `default` or `release` CMake preset
with an MSVC x64 toolchain. Build/run the `Toolbox` target; the test targets
are also available. The preset's Qt path is local and can be overridden.

## Structure

```text
app/             Application lifetime and ElaWindow integration
core/tool/       Tool descriptors, registry and page lifecycle
core/navigation/ Group/item configuration, validation and ordering
core/services/   AppSettings: validated settings and translation installation
ui/              LazyToolPage, SettingsPage and small Ela palette helpers
tools/encoding/  Base64 page and unchanged Base64Codec
tools/text/style/ Text style page, strict rule loader and deterministic engine
resources/styles/ Built-in style data and scoped data license
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
and choose **Analyze**. Click one of five candidate buttons (unused slots are disabled), inspect **Recovery process**
and copy the selected result. Editing the input invalidates old results; typing
does not automatically run analysis. Local analysis stays offline. Optional AI
assistance runs only when you explicitly click **AI deep analysis**.

The tested Qt 6.11.2 kit provides UTF-8, Latin-1, Windows-1252, GBK, GB18030,
Big5, Shift-JIS and EUC-KR via named QStringEncoder/QStringDecoder instances.
Strict conversions check exact byte/text round-trips. Other Qt kits may provide
fewer encodings; unavailable codecs are omitted at runtime.

Search is limited to two conversions, 100 unique candidates and a 12-state beam.
Scores are length-normalized quality indices from 0 to 100, with penalties for
conversion steps and incomplete recovery, plus a preference for UTF-8 decoding.
They are heuristics rather than calibrated probabilities. Plausible normal
text stays the preferred result; close candidates and lost information remain
uncertain. Mixed Western garbling can be repaired in representable segments,
but complete-text reversibility is explicitly marked unproven for such paths.
Damaged UTF-8-as-GBK/GB18030 paragraphs can also yield partial recovery:
readable sections are decoded, unknown byte regions remain marked `�`, and
the result is explicitly uncertain and not reversible. Search avoids paths
that introduce more anomalies or create fresh mojibake from CJK text.

### Single text files

Switch to **Text file** to load the original bytes (up to 10 MiB), compare up
to five candidates and save a repaired copy. File analysis additionally supports
UTF-16LE, UTF-16BE, UTF-32LE and UTF-32BE, giving twelve source/output encodings
on the tested Qt kit. BOM markers are recognized before decoding; without a BOM,
byte structure and text quality rank alternatives. You can explicitly choose a
source encoding when automatic comparison is inconclusive. An encoding that
passes validation does not prove the original meaning.

Already-garbled text stored in a valid encoding can undergo the existing local
repair search after decoding. This extra search is skipped when a decoded
candidate exceeds 1 MiB in UTF-8; direct decoding remains available up to the
10 MiB file limit. Analysis runs in the background with cancellation. The preview
shows at most 32,768 UTF-16 code units, while export uses the complete candidate.
File contents are not sent to AI.

Choose the output encoding, BOM and newline convention before saving. Defaults
are UTF-8, the source BOM's presence and preserved newlines. Unrepresentable
output characters cause an error instead of substitution. Saving requires a new
destination: existing files, including the source, are never overwritten. The
source's SHA-256 is checked again before export; changed sources require another
analysis. File-content batch conversion remains deferred.

See [manual file-recovery checks](docs/File-Recovery-Testing.md) for reproducible
fixtures and acceptance steps.

### File and folder names

Switch to **File and folder names** and choose a source directory. The app scans
the complete tree, hashes file bytes and generates name candidates locally.
Original names remain selected until you choose a candidate or apply a manual
name. Generated file-name candidates preserve the final extension. The tree
shows original names, proposed names and errors; changing a folder name updates
its descendants' planned output paths. Five candidate buttons and inverse paths
help compare alternatives. Unicode names offer less evidence than original
encoding bytes; even reversible suggestions can have the wrong meaning.

Windows-forbidden characters, device names, trailing spaces/dots and sibling
conflicts (case-insensitive, including file/folder conflicts) block export.
Review the plan, then choose **Create directory copy** and specify a new path
outside the source tree. Every scanned entry is copied, including unchanged
names, hidden entries and empty directories; file contents are not converted.
The output must not exist. The source tree is verified before and after copying,
and each copied file is checked against its scanned hash. File timestamps,
permissions and other filesystem metadata are not preserved by this feature.

Scanning and copying run in the background with cancellation and progress.
This stage limits a tree to 1,000 entries, 32 levels and 256 MiB of file contents;
links, junctions and special files cause the scan to stop rather than skip entries
silently. No names or contents are uploaded. Cancellation or errors after output
creation leave a clearly reported partial copy; another attempt requires a new
destination. Original entries are never renamed or deleted.

See [manual name-recovery checks](docs/Filename-Recovery-Testing.md).

### Clipboard sources

Switch to **Clipboard** and click **Read clipboard** to capture one snapshot.
The app does not monitor clipboard changes. It lists advertised MIME formats,
then compares plain text and text extracted from HTML side by side, including
anomaly and replacement-character counts. Plain text is selected when usable;
HTML is the fallback when no usable plain text is offered. Different strings
can reflect formatting differences as well as encoding damage: review them
before choosing a source.

HTML extraction runs in the background and preserves NBSP, line breaks and
Unicode text; external images, stylesheets and other resources are not loaded.
Non-text objects retain placeholders. RTF and unknown formats are listed but
their payloads are not retrieved or parsed. Each accepted text or HTML input is
limited to 1 MiB in UTF-8 (HTML markup counts toward the limit); previews show
at most 32,768 UTF-16 units. Clipboard providers may allocate/convert their data
before the app can reject an oversized offered representation.

**Use selected text** transfers the complete chosen text to the existing pasted
text page, invalidating previous results. It does not run analysis or call AI.
Run **Analyze** to compare five recovery candidates and inspect their paths;
AI still requires its own explicit action and sends only the current analysis
input and local candidates. Reading a snapshot does not replace pasted input.
Clearing it does not clear the system clipboard or your analysis input. The
text editor normalizes paragraph/newline separators; no original-byte claim is
made for clipboard sources. Even more readable HTML text is not proof of the
original meaning.

See [manual clipboard checks](docs/Clipboard-Testing.md).

### Diagnostic reports

All four sources share a diagnostic summary and **Export diagnostic report**
action. Reports distinguish original-byte evidence from Unicode text/names,
verified inverses from unverified/manual choices, blocked plans from usable
results, and possible loss from confirmed facts. Close candidate scores (within
five points), equivalent encoding aliases and differing clipboard representations
are ambiguity signals, not calibrated confidence estimates. A clipboard-source
report explicitly states that repair analysis has not run yet.

Export creates a new local JSON file using schema version 1. It records source
kind, assessment, metrics, evidence, candidate scores/paths and the selected
candidate. File reports include the source path and SHA-256; name-plan reports
include the source root, original/proposed names, parent indices, validation
errors and file hashes. They represent the analyzed snapshot, not a fresh scan
or proof that a later export succeeded. File bodies, pasted/clipboard text,
AI replies and API configuration are excluded. Report export does not run
analysis, call AI or overwrite an existing file.

Reports update after candidate/name/source selection. Editing text, clearing a
clipboard snapshot or starting a new analysis invalidates the old report.
See [manual diagnostic checks](docs/Diagnostic-Testing.md) for field meanings
and acceptance steps.

### Optional AI assistance

In **Settings → AI API**, enter your provider's HTTPS base URL (normally ending
in `/v1`), model identifier and API key, then save. The app uses the
[OpenAI-compatible Chat Completions protocol](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create),
appending `/chat/completions` unless already present. Saving settings sends no
request. HTTP is accepted only for loopback services; leave the key empty only
for services that do not require authentication. Redirects are not followed.

Enter the base URL and key, then click **Fetch models** to query the provider's
[model list](https://developers.openai.com/api/reference/resources/models/methods/list)
using `GET /models`. Choose an exact model ID from the editable list and save;
a provider/product name such as `MiMo` is not a model ID. Discovery sends only
authentication, never tool input, and does not require a model ID beforehand.
The current typed selection is preserved, and changing the address/key clears
the old catalog and cancels pending discovery. Services without a model-list
endpoint retain manual entry. Listed models may include non-chat models or models
outside your account's permissions; listing is not a guarantee of chat access.
MiMo also documents a [models endpoint](https://mimo.mi.com/docs/en-US/api/model/list-models).

Run local analysis first, then click **AI deep analysis**. The original text and
local candidates (including scores, paths and inverse verification) are sent to
the configured service. AI suggestions and explanations are displayed separately;
they never replace the five local candidates. Contextual completions are guesses,
not proven recovery of lost bytes. Invalid/empty JSON replies, authorization,
quota, connection and timeout failures are reported without displaying server
error bodies or API keys. Cancel is supported; editing text/settings discards
old suggestions and cancels in-flight requests.

Windows API keys are encrypted with current-user
[DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)
before storing them in settings. They are not saved in plaintext or written to
logs. This protects the stored configuration, not a compromised Windows account
or a provider to which you deliberately send the key. Non-Windows builds refuse
to persist nonempty keys rather than falling back to plaintext.

AI input is limited to 16 KiB, the complete request to 128 KiB, the response to
1 MiB, and the request duration to 90 seconds. A model must follow the requested
JSON structure. Replies are received as SSE streams with elapsed-time and
character-count progress; providers returning ordinary JSON remain supported.
Official MiMo endpoints receive `thinking.type=disabled` for this task, as
documented in [MiMo's thinking-mode guide](https://mimo.mi.com/docs/en-US/quick-start/usage-guide/other/deep-thinking).
Other providers do not receive this nonstandard parameter. A completed stream
is validated before showing a final suggestion; truncated output is rejected.
Real-provider behavior
must be verified with your configured service; automated tests use loopback only.

Regression coverage includes verified byte fixtures, Chinese/Japanese/Big5/Korean
cases, double mojibake, original preservation, invalid/lossy input, inverse paths,
size/performance bounds and asynchronous page lifecycle. `TestMojibake` results
are saved in the build's `tests/mojibake-results.txt`.

MSVC builds use a small compiler launcher to emit UTF-8 diagnostics and detect
the actual `/showIncludes` prefix. This avoids stale objects caused by localized
include output being parsed with the wrong codepage.

## License

MIT.
