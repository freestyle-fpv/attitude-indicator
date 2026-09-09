#!/usr/bin/env python3
"""Fetch only required official library sources, pinned and Git-blob verified."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
COMMIT = "e4344e70c2fa78a13e8a06566507f1ba8af6672a"
REPO = "waveshareteam/ESP32-S3-Touch-AMOLED-1.75"
LIBS = {"GFX_Library_for_Arduino", "SensorLib", "ESP32_IO_Expander", "XPowersLib", "esp-lib-utils"}


def get(url):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except Exception:
            if attempt == 3:
                raise
            time.sleep(attempt + 1)


def fetch(entry):
    relative = entry["path"].removeprefix("examples/arduino/libraries/")
    target = ROOT / "vendor" / relative
    def matches(data):
        return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest() == entry["sha"]
    if target.exists() and matches(target.read_bytes()):
        return
    data = get(f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/{entry['path']}")
    if not matches(data):
        raise RuntimeError(f"Hash mismatch: {relative}")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def main():
    tree = json.loads(get(f"https://api.github.com/repos/{REPO}/git/trees/{COMMIT}?recursive=1"))
    if tree.get("truncated"):
        raise RuntimeError("Incomplete repository tree")
    selected = []
    for entry in tree["tree"]:
        parts = entry["path"].split("/")
        if entry["type"] != "blob" or parts[:3] != ["examples", "arduino", "libraries"] or len(parts) < 5:
            continue
        if parts[3] in LIBS and (parts[4] == "src" or len(parts) == 5):
            selected.append(entry)
    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as pool:
        for count, _ in enumerate(pool.map(fetch, selected), 1):
            if count % 50 == 0:
                print(f"Verified {count}/{len(selected)} files", flush=True)
    (ROOT / "vendor" / "SOURCE.json").write_text(json.dumps({"repository": REPO, "commit": COMMIT, "files": selected}, indent=2))
    print(f"Official dependencies ready: {len(selected)} files at {COMMIT}")


if __name__ == "__main__":
    main()
