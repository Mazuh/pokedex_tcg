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
| Demo UI | `gui/views/assistant_prompt_dialog.{h,cpp}` | A modal prompt/answer box, opened from the sidebar footer and the Tools menu. |
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

## Webcam card-scanning — feasibility assessment (NOT implemented)

The motivating future feature: point a webcam at a physical card, have the assistant
read back the card **name and set** to autofill the "Add copy" form.

**Does the HTTP integration support it? Yes — the transport shape already fits.**

- Gemini's `generateContent` endpoint (the same one this module already POSTs to)
  accepts **image input** in the request body: an image part is
  `{"inline_data":{"mime_type":"image/jpeg","data":"<base64>"}}` placed in the same
  `contents[].parts` array next to the text part. No new endpoint, auth scheme, or
  transport is needed — only a richer request body. The OpenAI and Anthropic message
  APIs take images the same way, so this stays provider-neutral.
- The neutral `AiPrompt` DTO is deliberately left open to grow an image-parts field
  (e.g. `std::vector<AiImagePart>` of `{mimeType, bytes}`), which `GeminiAssistant`
  would base64-encode into `inline_data`. That is the *only* core change the wire
  side would need.
- Structured output: asking for the name/set as JSON (a `responseMimeType:
  application/json` hint, or just parsing a constrained reply) keeps autofill robust.

**What is genuinely out of scope here (and unbuilt):**

- **Webcam capture.** Qt Multimedia (`QCamera` / `QMediaCaptureSession` /
  `QVideoSink`) would grab a frame → `QImage` → JPEG bytes. That is a new GUI
  dependency (`Qt6::Multimedia`) and a new capture surface; **not added**.
- **Image encoding / downscaling** before upload (cost + latency control).
- **The autofill flow**: mapping the model's answer back onto the finder/search so it
  resolves to a real `CardReference` (name + set + number). The assistant's reading
  would still need to go **through the existing card search** to become a trustworthy
  printing, not be trusted verbatim.
- **Cost / rate awareness** for image calls (larger, pricier than text).

Bottom line: the HTTP layer and the seam are ready for multimodal with a minimal,
additive change; the missing pieces are all capture/UX, not transport.
