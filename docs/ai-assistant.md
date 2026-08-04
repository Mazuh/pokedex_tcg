# AI assistant module

A small, vendor-neutral integration with a text LLM. Gemini is the concrete
provider today, but nothing outside one class knows that — swapping providers is a
one-line change at the composition root.

## Shape

The module follows the same seam pattern as the card catalog (`CardCatalogApi` +
`PokemonTcgIoApi` + a GUI transport):

| Layer | File | Role |
|-------|------|------|
| Core seam (Qt-free) | `core/app/ai_assistant.h` | `AiAssistant` abstract interface + neutral DTOs (`AiPrompt`, `AiRequest`, `AiResult`). Builds the HTTP request, parses the response. No I/O, no clock — headlessly unit-testable. |
| Concrete provider | `core/app/gemini_assistant.{h,cpp}` | `GeminiAssistant` — the **only** place that knows Gemini's URL scheme, JSON request shape, and response layout. |
| GUI transport | `gui/services/assistant_service.{h,cpp}` | `AssistantService` (QObject) — reads the API key from config, POSTs via the `loggedPost` chokepoint, emits `answerReady` / `failed`. Never names the provider. |
| Demo UI | `gui/views/assistant_prompt_dialog.{h,cpp}` | A modal prompt/answer box, opened from the Tools menu. |
| Card reader (core) | `core/app/card_scan.{h,cpp}` | Qt-free: the card-scan **system instruction**, the vision-prompt builder (`buildCardScanPrompt`), and the tolerant JSON **reply parser** (`parseScannedCard` → `ScannedCard`). No I/O, unit-tested. |
| Scan UI | `gui/views/scan_card_view.{h,cpp}` | An in-window webcam screen (QCamera / QMediaCaptureSession / QImageCapture / QVideoWidget), pushed into the shell's section stack with a Back top bar — not a modal: capture a frame → freeze the still → send it as a vision prompt → show the reading as editable fields → `cardResolved()`. |
| Config | key `assistant_api_key` in the `key=value` config file | The provider secret. Entered (masked) in **Settings**, read fresh per call so it applies live. |
| Config | key `assistant_model` (optional) | Overrides which model the provider queries — e.g. a pinned version like `gemini-3.6-flash` — whatever the account can access. Absent = `GeminiAssistant`'s built-in default, the rolling alias `gemini-flash-latest` (always the current GA Flash, so it never breaks when a specific version is retired/gated). Read once at startup in `main.cpp`. |

### Why both build *and* parse live behind the interface

Unlike the card catalog (where parsing is a free function), the `AiAssistant` seam
owns both request-building and response-parsing, because **both** the request shape
and the response shape are provider-specific. To swap providers you write a sibling
of `GeminiAssistant` and change the single construction line in `main.cpp`; the
transport, the config key, the Settings field, and the demo UI are untouched.

### Secret handling

The API key is sent in the `x-goog-api-key` **header**, never the URL, so it never
reaches the network log (`loggedPost` logs only the URL, never headers or body). It
is stored on the user's machine in the app config file, in plain text — the same
place and treatment as the workspace path (no OS keychain integration today).

## Webcam card-scanning (implemented)

Point the webcam at a physical card, have the assistant **read the print**, and use
what it read to search the collection. Opened from the sidebar footer's **"✦ Scan
card"** button (and Tools ▸ *Scan a card…*).

**The division of labor is the important design choice.** The assistant does **not**
"find" the card — it only *reads* the print and hands back a plain **search string**
(plus the components it read). The app's own deterministic search does the matching.
This keeps the LLM out of the trust path for identity: a misread never silently files
the wrong card, it just yields a search the user reviews.

### Flow

1. **Capture.** `ScanCardView` shows a live `QVideoWidget` preview; pressing *Scan*
   grabs a frame via `QImageCapture`, then **freezes** it — the camera stops and the
   captured still replaces the preview (a `QStackedWidget` swap) so the user sees exactly
   what was sent; a *Retake* button resumes the live camera. The frame is downscaled to
   ≤1024px and JPEG-encoded to base64 (cost + latency control) GUI-side.
2. **Read.** `buildCardScanPrompt(base64)` (core) builds a vision `AiPrompt` — the
   image part, the card-scan **system instruction**, and `wantsJsonResponse=true`.
   `AssistantService::ask(const AiPrompt&)` POSTs it through the same transport as the
   text demo.
3. **Parse.** `parseScannedCard(reply)` (core) tolerantly parses the JSON into a
   `ScannedCard { identified, cardName, setName, setCode, collectorNumber, query,
   note }`. It strips ``` fences / prose, never throws, and — for an identified card
   that omitted the query — synthesizes one from set + number. An unreadable card is
   `identified=false` with a `note` the view shows so the user can reposition.
4. **Review.** The reading is shown split in half: on the **left**, **editable fields**
   (Card / Set / Set code / Number / Search) so the user can fix a misread letter or digit
   before they're used; on the **right**, **"first impressions"** — a muted owned-name
   match count ("N of your cards may match this name") as a "have I already added this?"
   read (MainWindow supplies a snapshot matcher via `ScanCardView::setOwnedNameMatcher`, so
   the view stays storage-free), and the **detected Pokémon** (species name, dex #, region)
   via `detectScannedSpecies` (core, Qt-free: whole-word token match of the card name
   against the National Pokédex catalog — "Ash's Pikachu"→Pikachu, "Mewtwo"≠Mew, "Parasol
   Lady"→nothing).
5. **Search (optional).** On *Search my cards* the view emits `cardResolved(ScannedCard)`
   carrying the fields **as edited**; `MainWindow` fills the **My Cards** live search with
   `query` (so the user sees whether they already own it) and switches there. From there
   the next *Add a card* consumes the stash (`OwnedCardsView` → `AddCardCopyPage::prefillFrom`).
6. **Add directly.** Two right-panel buttons skip the search — for a booster where the user
   already knows they don't own the card — both via `addRequested(reading, dexNumber,
   copyFieldsToForm)`, routed by MainWindow to the species add page (detected dex) or the
   species-free one:
   - **"Create by set" / "Create by name"** (`copyFieldsToForm=false`): seed the catalog
     finder search and let the picked printing autofill — species by set
     (`openAddCopyBySet` → `prefillSetSearch`, set code or full-name fallback when the code
     is too short to auto-search), non-Pokémon by name (`startAddScannedCard(…, false)` →
     `prefillFrom`). The reliable path when the search works.
   - **"Copy to creation form"** (`copyFieldsToForm=true`): paste the read fields straight
     onto the form with **no catalog search** (`prefillFormFields`) — the escape hatch when
     the card search is flaky. The user reviews/edits and saves.

### Wire + config notes

- Gemini's `generateContent` (the same endpoint the text path uses) takes the image as
  `{"inline_data":{"mime_type":"image/jpeg","data":"<base64>"}}` in `contents[].parts`;
  the neutral `AiPrompt` grew an `images` field (`std::vector<AiImagePart>` of
  `{mimeType, base64Data}`) that `GeminiAssistant` folds in. The base64 is done GUI-side
  (Qt's codec) so core needs no base64 implementation. Provider-neutral: OpenAI/Anthropic
  take images the same way.
- `wantsJsonResponse` maps to Gemini's `generationConfig.responseMimeType:
  application/json` — a belt to the system-instruction's "reply with JSON" suspenders.
- New GUI dependency: **`Qt6::Multimedia` + `Qt6::MultimediaWidgets`** (Ubuntu CI adds
  `qt6-multimedia-dev`). Core stays Qt-free — all the card-scan logic is in `card_scan`.

### macOS camera permission

macOS gates camera access behind a per-app TCC permission. Getting the prompt to fire
took **two build-side things**, and missing either one silently auto-denies (the trap we
fell into — the failure is only a log line, never a prompt or a Settings entry). The macOS
build is therefore a real **`.app` bundle**, not a bare binary (`if(APPLE)` block in
`CMakeLists.txt`):

- **A real bundle `Info.plist` with `NSCameraUsageDescription`.** Qt's camera permission
  handler reads the usage description from `[NSBundle mainBundle].infoDictionary`, and
  LaunchServices/TCC register the app by its bundle. A bare executable — even one with an
  embedded `__TEXT,__info_plist` section — does **not** reliably satisfy Qt's request
  (*"Could not request QCameraPermission … include the required usage description"*) and
  never registers (`tccutil` couldn't even find its bundle id, and it never appeared in
  System Settings ▸ Camera). `MACOSX_BUNDLE` + `cmake/macos/Info.plist.in` → a proper
  `Contents/Info.plist` fixes all of that. The bundle is ad-hoc-signed (POST_BUILD
  `codesign --force --sign -`) with the stable identifier `com.mazuh.pokedex-tcg` so TCC
  attributes/remembers the grant; no developer certificate needed.
- **Import the static permission plugin.** Homebrew's Qt is a **static** build, so its
  permission backends are `.a` plugins that only exist at runtime when explicitly
  imported — and Qt does **not** auto-import permission plugins (opt-in, each needs a
  usage description). Without `qt_import_plugins(pokedex_tcg INCLUDE
  Qt6::QDarwinCameraPermissionPlugin)`, `QCameraPermission` has no handler at all
  (*"Could not find permission plugin for QCameraPermission"*).

Because it's a bundle, the binary lives at `build/pokedex_tcg.app/Contents/MacOS/
pokedex_tcg`; `dev.sh` runs that inner path (recognized as the bundle, stdout still in the
terminal), `install.sh` installs the whole `.app` plus a `pokedex` symlink into it, and the
README points at the `.app`.

- **Code side.** `ScanCardView::startCamera()` calls `qApp->checkPermission(
  QCameraPermission{})` and, when *Undetermined*, `requestPermission(...)` — which shows
  the native prompt. Every outcome is shown **in the dialog**, not just the log:
  *Undetermined* → "Waiting for camera permission…"; *Denied* → a message telling the
  user to enable it in **System Settings ▸ Privacy & Security ▸ Camera**; no camera → its
  own message. (Before this, a denied camera only produced a `qt.permissions` /
  "Access to camera not granted" **log line** a regular user would never see.)

**Granting / resetting it (as a user or when testing):**

- First scan shows the OS prompt — click *Allow*. Or enable it later in **System Settings
  ▸ Privacy & Security ▸ Camera**.
- To re-trigger the prompt during development: `tccutil reset Camera com.mazuh.pokedex-tcg`
  (works once the bundle has been launched at least once, so LaunchServices knows the id;
  `tccutil reset Camera` resets all apps).
- Caveat: an ad-hoc-signed dev bundle's signing hash changes on rebuild, so macOS may
  re-prompt after a rebuild. A distributable build would use a Developer ID signature,
  which makes the grant stable.

### Known limitations / future work

- **No image is persisted from the scan** — the captured frame is used only for the
  reading and discarded. Saving it as the copy's image (instead of re-searching the
  catalog) is a possible enhancement.
- **Cost / rate awareness** for image calls (larger than text) is not specially
  handled beyond the downscale; scans are strictly user-initiated (one per *Scan*
  press), so there is no background image traffic.
