// Kiri Bridge — esbuild bundle script.
// Compiles Preact+TS sources to a single JS bundle and a single CSS file.
// Outputs to dist/, where build_assets.py picks them up for chunking + gzip.

import { build } from "esbuild";
import { mkdirSync, existsSync, rmSync, writeFileSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = resolve(__dirname, "..");
const distDir = resolve(root, "dist");

const args = new Set(process.argv.slice(2));
const target = args.has("--target") ? process.argv[process.argv.indexOf("--target") + 1] : "main";

if (existsSync(distDir)) rmSync(distDir, { recursive: true, force: true });
mkdirSync(distDir, { recursive: true });

const common = {
  bundle: true,
  format: target === "installer" ? "iife" : "esm",
  platform: "browser",
  target: ["es2020"],
  minify: true,
  sourcemap: false,
  legalComments: "none",
  jsx: "automatic",
  jsxImportSource: "preact",
  treeShaking: true,
  loader: { ".css": "css" },
  drop: ["console", "debugger"],
};

const entry = target === "installer"
  ? resolve(root, "src/installer.tsx")
  : resolve(root, "src/main.tsx");

const outJs = resolve(distDir, target === "installer" ? "installer.js" : "app.js");
const outCss = resolve(distDir, target === "installer" ? "installer.css" : "app.css");

const result = await build({
  ...common,
  entryPoints: [entry],
  outfile: outJs,
  metafile: true,
});

const jsBytes = readFileSync(outJs).byteLength;
let cssBytes = 0;
if (existsSync(outCss)) cssBytes = readFileSync(outCss).byteLength;

const gzipSize = (path) => {
  const { gzipSync } = require("node:zlib");
  return gzipSync(readFileSync(path), { level: 9 }).byteLength;
};

const { gzipSync } = await import("node:zlib");
const gz = (path) => gzipSync(readFileSync(path), { level: 9 }).byteLength;

const jsGz = gz(outJs);
const cssGz = existsSync(outCss) ? gz(outCss) : 0;

console.log(`[bundle] target=${target}`);
console.log(`[bundle]   js  ${jsBytes.toLocaleString()} B  /  gz ${jsGz.toLocaleString()} B`);
if (cssBytes) console.log(`[bundle]   css ${cssBytes.toLocaleString()} B  /  gz ${cssGz.toLocaleString()} B`);

// Size guard. Adjust thresholds as the app grows; this catches accidental bloat
// (eg someone imports moment.js by mistake).
const JS_GZ_LIMIT = 30 * 1024;
const CSS_GZ_LIMIT = 8 * 1024;
if (jsGz > JS_GZ_LIMIT) {
  console.error(`[bundle] FAIL: js gzip ${jsGz} exceeds ${JS_GZ_LIMIT}`);
  process.exit(1);
}
if (cssGz > CSS_GZ_LIMIT) {
  console.error(`[bundle] FAIL: css gzip ${cssGz} exceeds ${CSS_GZ_LIMIT}`);
  process.exit(1);
}

writeFileSync(resolve(distDir, "meta.json"), JSON.stringify({
  target,
  js: { path: outJs.replace(root + "/", ""), bytes: jsBytes, gz: jsGz },
  css: cssBytes ? { path: outCss.replace(root + "/", ""), bytes: cssBytes, gz: cssGz } : null,
}, null, 2));
