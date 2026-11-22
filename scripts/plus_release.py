#!/usr/bin/env python3
"""Package a Cinemania Plus release ZIP and add it to the TMLoader feed.

Used by .github/workflows/plus-release.yml. The upstream package.py and
release.py are for the original Cinemania and are not used here.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zipfile

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent.parent
PRODUCT_ID = "CinemaniaPlus"
PRODUCT_NAME = "Cinemania Plus"
AUTHOR = "cosine2x (based on Cinemania by mediocre-q)"
DESCRIPTION = ("Cinemania with optional NVIDIA GPU (NVENC) video encoding for "
               "TrackMania United Forever 2.12.0. Do not enable together with Cinemania.")
GAME_VERSION = "2.12.0"
FIXED_TIME = (2026, 1, 1, 0, 0, 0)  # keeps ZIP bytes stable for identical inputs


def machine_type(data):
    """Return the PE machine type, or raise ValueError if this is not a PE file."""
    if data[:2] != b"MZ" or len(data) < 0x40:
        raise ValueError("not a PE file")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[offset:offset + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    return struct.unpack_from("<H", data, offset + 4)[0]


def read_checked(path, machine=None, minimum_size=1):
    data = path.read_bytes()
    if len(data) < minimum_size:
        raise ValueError(f"{path} is unexpectedly small")
    if machine is not None and machine_type(data) != machine:
        raise ValueError(f"{path} has the wrong CPU architecture")
    return data


def build_zip(output, entries):
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(entries):
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, entries[name])


def update_feed(feed_path, repo, version, digest):
    if feed_path.exists():
        feed = json.loads(feed_path.read_text(encoding="utf-8"))
    else:
        feed = {"api": 1, "game": "TmForever", "products": {}}
    if feed.get("api") != 1 or feed.get("game") != "TmForever" or not isinstance(feed.get("products"), dict):
        raise ValueError("invalid TMLoader feed")
    product = feed["products"].setdefault(PRODUCT_ID, {
        "name": PRODUCT_NAME, "author": AUTHOR, "type": "modification",
        "description": DESCRIPTION, "homepage": f"https://github.com/{repo}", "versions": {}})
    entry = {
        "executable": "Cinemania.dll",
        "dependencies": [{"id": "TmForever", "version": GAME_VERSION}],
        "download_link": f"https://github.com/{repo}/releases/download/v{version}/Cinemania-Plus-{version}.zip",
        "hash_sha256": digest,
        "changelog": f"Cinemania Plus {version}: adds NVIDIA GPU (NVENC) encoding with automatic CPU fallback.",
    }
    if version in product["versions"]:
        raise ValueError(f"version {version} is already in the feed")
    product["versions"][version] = entry
    feed_path.write_text(json.dumps(feed, indent=4, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repo", required=True, help="GitHub owner/name, e.g. cosine2x/Cinemania-Plus")
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    parser.add_argument("--ffmpeg-license", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--feed", type=Path, required=True)
    args = parser.parse_args()
    try:
        if not re.fullmatch(r"(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*)){2}", args.version):
            raise ValueError("version must look like 1.1.0")
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repo):
            raise ValueError("repo must look like owner/name")
        entries = {
            "Cinemania.dll": read_checked(args.dll, 0x14C, 1000),          # 32-bit: the game is 32-bit
            "bin/CinemaniaWorker.exe": read_checked(args.worker, 0x8664, 1000),
            "bin/ffmpeg.exe": read_checked(args.ffmpeg, 0x8664, 10_000_000),
            "LICENSE": read_checked(ROOT / "LICENSE"),
            "licenses/MinHook-LICENSE.txt": read_checked(ROOT / "third_party/minhook/LICENSE.txt"),
            "licenses/FFmpeg-LICENSE.txt": read_checked(args.ffmpeg_license),
            "licenses/PLUS-NOTICE.md": read_checked(ROOT / "licenses/PLUS-NOTICE.md"),
        }
        args.output_dir.mkdir(parents=True, exist_ok=True)
        archive = args.output_dir / f"Cinemania-Plus-{args.version}.zip"
        build_zip(archive, entries)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        update_feed(args.feed, args.repo, args.version, digest)
        print(f"{archive}\nSHA-256: {digest}")
    except (OSError, ValueError, KeyError, json.JSONDecodeError, FileExistsError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
