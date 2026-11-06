# F1 upload names (REV-6 A1)

Changed: music rows, breadcrumb links, background rows and playlist rows use DOM textContent/dataset properties and click listeners. Background src is assigned as a DOM property with an encoded path. Names never enter inline JavaScript or concatenated HTML attributes. Requests, buttons and native confirmation/rename dialogs retain their existing behavior.

Other name sinks: playlist song titles/folders/missing entries and upload queue names/results/errors use esc() in HTML text; WiFi status uses textContent (saved WiFi notice uses escaped text). Delete/error dialogs use confirm/alert and rename uses prompt. Button titles are fixed literals. Numeric playlist controls retain index-only inline handlers.

JSON: all name-bearing API strings use jsonEscape; quotes and backslashes were already escaped, control bytes become spaces. Added slash escaping so closing-tag sequences become <\/script>; JSON.parse preserves paths. Responses use application/json; shared sendJson now adds X-Content-Type-Options: nosniff, including streamed listing/song responses. These headers supplement the DOM fix.

Checks (no device operations):
- node tests/upload_xss.test.cjs: PASS upload XSS: hostile names stay inert; listing, breadcrumbs, background, playlist and action paths preserved
- powershell -NoProfile -File tools/check.ps1 -SkipProbe -SkipHostMedia: exit 0.
- Same command with -Board jc2432w328c: exit 0.
Both: PASS: compile-only checks completed; no device commands were run.
Logs: .tools/logs/f1-xss.log, f1-default.log, f1-jc2432w328c.log.

Open decisions: none. The focused test executes embedded page JavaScript with a minimal DOM model, not a real browser; slash-bearing attack strings are tested as synthetic API names (literal slashes delimit real filesystem paths).
