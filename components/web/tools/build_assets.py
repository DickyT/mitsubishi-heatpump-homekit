#!/usr/bin/env python3
# Kiri Bridge — build gzip-compressed WebUI asset fragments for ESP-IDF embedding.
#
# Input: pages/loader.js (template) + pages/app.js + pages/app.css produced by esbuild.
# Output: per-chunk gzipped assets in <out-dir> + a generated cpp file that exposes
# them through web_assets::find(path).

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path

MAX_CHUNK_BYTES = 900


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_leading_source_header(text: str) -> str:
    text = text.lstrip()
    for pattern in (r"^/\*.*?\*/\s*", r"^<!--.*?-->\s*", r"^(?:# .*\n)+\s*"):
        text = re.sub(pattern, "", text, count=1, flags=re.DOTALL)
    return text


def compact_js(text: str) -> str:
    # esbuild already minifies app.js. We only strip the loader.js source header
    # so we don't accidentally double-minify the bundled output.
    text = strip_leading_source_header(text)
    return text


def chunk_text(text: str, max_bytes: int = MAX_CHUNK_BYTES) -> list[str]:
    chunks: list[str] = []
    current: list[str] = []
    current_bytes = 0

    for char in text:
        char_bytes = len(char.encode("utf-8"))
        if current and current_bytes + char_bytes > max_bytes:
            chunks.append("".join(current))
            current = []
            current_bytes = 0
        current.append(char)
        current_bytes += char_bytes

    if current:
        chunks.append("".join(current))
    return chunks or [""]


def gzip_bytes(text: str) -> bytes:
    return gzip.compress(text.encode("utf-8"), compresslevel=9, mtime=0)


def asset_name(asset_path: str) -> str:
    name = asset_path.strip("/")
    for old, new in (("/", "_"), (".", "_"), ("-", "_")):
        name = name.replace(old, new)
    return "asset_" + name + "_gz"


def write_asset(out_dir: Path, url_path: str, text: str, content_type: str, assets: list[dict]) -> None:
    name = asset_name(url_path)
    file_path = out_dir / name
    file_path.write_bytes(gzip_bytes(text))
    assets.append({
        "url": url_path,
        "name": name,
        "file": str(file_path),
        "content_type": content_type,
        "bytes": file_path.stat().st_size,
    })


def add_chunked_assets(out_dir: Path,
                       prefix: str,
                       suffix: str,
                       text: str,
                       content_type: str,
                       assets: list[dict]) -> list[str]:
    paths: list[str] = []
    for index, chunk in enumerate(chunk_text(text)):
        url_path = f"/assets/{prefix}.{index}.{suffix}"
        write_asset(out_dir, url_path, chunk, content_type, assets)
        paths.append(url_path)
    return paths


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def generate_cpp(out_dir: Path, assets: list[dict], version: str) -> Path:
    cpp_path = out_dir / "web_assets_generated.cpp"

    externs: list[str] = []
    rows: list[str] = []
    for asset in assets:
        name = asset["name"]
        externs.append(f'extern const uint8_t {name}_start[] asm("_binary_{name}_start");')
        externs.append(f'extern const uint8_t {name}_end[] asm("_binary_{name}_end");')
        rows.append(
            "    {"
            f"{cpp_string(asset['url'])}, "
            f"{name}_start, "
            f"{name}_end, "
            f"{cpp_string(asset['content_type'])}"
            "},"
        )

    cpp_path.write_text(
        "\n".join([
            '#include "web_assets.h"',
            "",
            "#include <cstring>",
            "",
            *externs,
            "",
            "namespace web_assets {",
            "",
            "namespace {",
            "constexpr GzipAsset kAssets[] = {",
            *rows,
            "};",
            "}",
            "",
            "const GzipAsset* find(const char* path) {",
            "    if (path == nullptr) {",
            "        return nullptr;",
            "    }",
            "    for (const GzipAsset& asset : kAssets) {",
            "        if (std::strcmp(asset.path, path) == 0) {",
            "            return &asset;",
            "        }",
            "    }",
            "    return nullptr;",
            "}",
            "",
            "const char* version() {",
            f"    return {cpp_string(version)};",
            "}",
            "",
            "}  // namespace web_assets",
            "",
        ]),
        encoding="utf-8",
    )
    return cpp_path


def generate_manifest_cmake(out_dir: Path, assets: list[dict], cpp_path: Path) -> Path:
    manifest_path = out_dir / "web_assets_manifest.cmake"
    files = "\n".join(f'    "{asset["file"]}"' for asset in assets)
    manifest_path.write_text(
        "\n".join([
            f'set(WEB_ASSETS_CPP "{cpp_path}")',
            "set(WEB_GZIP_ASSET_FILES",
            files,
            ")",
            "",
        ]),
        encoding="utf-8",
    )
    return manifest_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pages-dir", required=True, help="Directory holding loader.js + app.js + app.css")
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()

    pages_dir = Path(args.pages_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    loader_path = pages_dir / "loader.js"
    app_js_path = pages_dir / "app.js"
    app_css_path = pages_dir / "app.css"
    for path in (loader_path, app_js_path, app_css_path):
        if not path.exists():
            raise SystemExit(f"Missing input asset: {path}")

    digest = hashlib.sha256()
    digest.update(b"content-encoding:gzip")
    for path in (loader_path, app_js_path, app_css_path):
        digest.update(path.name.encode("utf-8"))
        digest.update(path.read_bytes())
    version = digest.hexdigest()[:12]

    assets: list[dict] = []
    manifest = {"version": version, "css": [], "js": []}
    manifest["css"] = add_chunked_assets(out_dir, "app", "css", read_text(app_css_path), "text/css; charset=utf-8", assets)
    manifest["js"] = add_chunked_assets(out_dir, "app", "js", read_text(app_js_path), "application/javascript; charset=utf-8", assets)

    loader_template = compact_js(read_text(loader_path))
    loader = loader_template.replace("__WEB_ASSET_MANIFEST__", json.dumps(manifest, separators=(",", ":"), ensure_ascii=False))
    write_asset(out_dir, "/assets/loader.js", loader, "application/javascript; charset=utf-8", assets)

    cpp_path = generate_cpp(out_dir, assets, version)
    generate_manifest_cmake(out_dir, assets, cpp_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
