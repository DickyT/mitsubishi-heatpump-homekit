# Kiri Bridge WebUI

The Preact + esbuild source for every page the firmware serves:

- **Production WebUI** (`src/main.tsx` → `dist/app.js` + `dist/app.css`) is the
  SPA loaded over HTTP at `http://<device>:8080/` for the `control`, `logs`,
  and `admin` pages. Bundled, brotli-compressed, chunked into rodata, and
  re-stitched in the browser by the bootstrap loader (`components/web/pages/loader.js`).
- **Installer captive portal** (`src/installer.tsx` → `dist/installer.html`)
  is a single self-contained page, embedded as a binary blob into the installer
  firmware (`debug_apps/cn105_probe/main/app_main.cpp`).

Both targets share `src/styles.css` and the dark/orange/mono visual language
that matches `kiri.dkt.moe`.

## First-time setup

```bash
cd webui
npm install
```

## Build

The firmware build runs these automatically via CMake at `idf.py build` time;
manual invocation is only needed when you want to test bundle output without a
firmware build.

```bash
npm run build              # production SPA → dist/app.js + dist/app.css
npm run build:installer    # installer page → dist/installer.html
```

## Architecture

```
src/                       What it produces                  Where it lives
├── main.tsx               dist/app.js                       /assets/app.*.js  (chunked)
├── styles.css             dist/app.css + installer inline   /assets/app.*.css (chunked)
├── installer.tsx          dist/installer.html               cpp BINARY embed
├── store.ts               singleton signals shared by pages
├── api.ts                 typed fetch wrappers
├── router.ts              path-based hash router
├── components.tsx         Modal / Section / Field / Btn primitives
├── layout.tsx             top sticky / bottom floating tabs + footer
├── types.ts               JSON contracts (mirror of web_routes.cpp)
└── pages/
    ├── Control.tsx        live state + remote, with draft-lock semantics
    ├── Logs.tsx           file list, preview, live tail
    └── Admin.tsx          settings, OTA, NVS editor, HomeKit QR, maintenance
```

The bundle pipeline:

```
src/*.tsx ──esbuild──► raw bundle ──terser──► minified
                                         │
              ┌──────────────────────────┴────────────┐
              │                                       │
   webui/dist/app.{js,css}            webui/dist/installer.html
              │                                       │
              ▼                                       ▼
   build_assets.py (chunk + brotli + emit cpp)   target_add_binary_data
              │                                       │
              ▼                                       ▼
        rodata in main firmware          rodata in installer firmware
```

## Tech decisions (and the dead ends)

### Preact, not React

10× smaller, drop-in. With the strict TS config in `tsconfig.json`,
`jsxImportSource: "preact"` and the matching esbuild option, every component
behaves like React with hooks. `@preact/signals` provides the cross-page
status singleton (`store.ts`).

### esbuild + terser, not Vite/Rollup

esbuild bundles in one step. Terser then runs as a second pass for an extra
~3% gz win. Build is sub-second on cold cache.

### Brotli over gzip — tried and reverted

Tried in commit `5935819` (~15% rodata saving on paper, 38.6 KB → 32.8 KB
across the chunked SPA), reverted in commit `fec53f7`. Theory said any
2020+ browser handles `Content-Encoding: br` natively. Reality on this
specific stack did not:

- `esp_http_server` does not natively negotiate `Accept-Encoding`. We
  send `Content-Encoding: br` unconditionally and assume the client copes.
- We chunk-and-stitch (86 small brotli streams concatenated client-side
  via the loader). Some clients refuse to decompress when the streams are
  smaller than the brotli window, others stall on the in-flight assembly.
- Captive-portal connectivity probes (iOS, Android, ChromeOS) sometimes
  call into the `/` shell endpoint with no `Accept-Encoding` header and
  treat the brotli body as broken HTML.

The decoder version stamp now folds the encoding name in
(`digest.update(b"content-encoding:gzip")`) so a future swap will bust
caches automatically. If we revisit, do it as `Accept-Encoding`-aware
content negotiation in `sendAsset` (ship both `_gz` and `_br` symbols,
pick at request time) — not as a unilateral switch.

### Bundle size guards

`tools/bundle.mjs` enforces ceilings so a stray `import "lodash"` can't
silently bloat the firmware:

| Target    | Limit      | Current  |
|-----------|------------|----------|
| main JS   | 30 KB gz   | 19.3 KB  |
| main CSS  | 8 KB gz    | 3.4 KB   |
| installer | 18 KB gz   | 13.2 KB  |

Builds fail loudly when the limit is crossed. Embedded chunked rodata
sits around 38–39 KB total (gzip).

### What we tried and reverted

- **Aggressive terser** (`unsafe_*`, `booleans_as_integers`, `toplevel`
  mangle, `passes: 3`) — produced smaller raw bytes but **larger gz output**
  by ~250 B because the more "compact" patterns compressed worse. Real-world
  byte counts beat theoretical minifier flags. See the `TERSER_OPTS` comment
  in `tools/bundle.mjs`.

- **lightningcss as a CSS post-pass** — added ~90 B to the gzipped CSS
  because it expanded shorthand colors / hex notation. Reverted; esbuild's
  built-in CSS minify is competitive enough at our size.

### Element-id contract with the firmware

Server endpoints are defined in `components/web/web_routes.cpp` and the
installer's `app_main.cpp`. Field names in API requests (`device_name`,
`wifi_ssid`, `cn105_baud`, etc.) are mirrored in `types.ts`. When you change
a server endpoint, update `api.ts` first and let TypeScript find the callers.

### Two-firmware reality

- `components/web/pages/loader.js` bootstrap is the **only** code shipped in
  source form (it inlines a manifest at build time). Keep it minimal — every
  byte runs before the SPA paints anything.
- The installer firmware never reaches the chunked pipeline; it ships one
  static blob because it must work without internet.

### draft-lock invariant on the Control page

While the user edits the form, polled status from `/api/status` does **not**
overwrite their pending input. The lock auto-clears once the server reflects
the user's last commit. Implemented in `pages/Control.tsx` with a
`draftLocked` ref + `pollPaused` signal.

## Common edits

- **New API endpoint** — add the response type in `types.ts`, the wrapper in
  `api.ts`, then call from a page.
- **New component** — drop into `components.tsx` if it's reusable, otherwise
  keep it inline in the page that uses it.
- **Style change** — edit `styles.css`. esbuild bundles it directly via the
  `import "./styles.css"` in `main.tsx` and `installer.tsx`.
- **New page** — add to `pages/`, register in `main.tsx` and `router.ts`.

## License

GPL-3.0 (matches the rest of the firmware).
