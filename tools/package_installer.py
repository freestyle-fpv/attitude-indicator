#!/usr/bin/env python3
"""Build a portable, offline USB installer from the Arduino build output."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "firmware"
SKETCH = "AttitudeIndicator.ino"
CORE = ROOT / ".arduino/data/packages/esp32/hardware/esp32/3.3.10"
WEB_TOOLS_VERSION = "10.5.2"
INSTALLER_REVISION = 2
WEB_TOOLS_URL = f"https://registry.npmjs.org/esp-web-tools/-/esp-web-tools-{WEB_TOOLS_VERSION}.tgz"
WEB_TOOLS_SHA512 = (
    "i5qsHv1bnrb/9gmKEN7qIaDTMOMy6OctrgFVXStrU2s79erY2Z7IKydveNk0"
    "eDhQTarfwyR4eXspOUIF6kYawg=="
)
SECTOR = 4096


def firmware_parts():
    merged = (BUILD / f"{SKETCH}.merged.bin").read_bytes()
    if len(merged) != 16 * 1024 * 1024:
        raise ValueError("Expected the 16 MiB ESP32-S3 build from tools/build.sh")

    # Keep the four writes separate so updating an existing installation
    # does not erase the NVS sectors between the partition table and OTA data.
    segments = (
        (0x0000, 0x8000, BUILD / f"{SKETCH}.bootloader.bin"),
        (0x8000, 0x9000, BUILD / f"{SKETCH}.partitions.bin"),
        (0xE000, 0x10000, CORE / "tools/partitions/boot_app0.bin"),
        (0x10000, 0x310000, BUILD / f"{SKETCH}.bin"),
    )
    parts = []
    for offset, limit, path in segments:
        data = path.read_bytes()
        if not data or offset + len(data) > limit:
            raise ValueError(f"Unexpected image size: {path.name}")
        if merged[offset:offset + len(data)] != data:
            raise ValueError(f"{path.name} differs from Arduino's merged image at {offset:#x}")
        if offset in (0, 0x10000) and (data[:1] != b"\xe9" or data[12:14] != b"\x09\x00"):
            raise ValueError(f"{path.name} is not an ESP32-S3 image")
        parts.append({
            "offset": offset,
            "sha256": hashlib.sha256(data).hexdigest(),
            "data": base64.b64encode(data).decode("ascii"),
        })

    table = (BUILD / f"{SKETCH}.partitions.bin").read_bytes()
    partitions = {}
    for entry in range(0, len(table) - 31, 32):
        magic, _, _, offset, size, name, _ = struct.unpack_from("<HBBII16sI", table, entry)
        if magic != 0x50AA:
            break
        partitions[name.rstrip(b"\x00")] = (offset, size)
    if (partitions.get(b"nvs") != (0x9000, 0x5000)
            or partitions.get(b"otadata") != (0xE000, 2 * SECTOR)
            or partitions.get(b"app0") != (0x10000, 0x300000)):
        raise ValueError("Unexpected partition layout; expected app3M_fat9M_16MB")
    return parts


def build_web_tools(esbuild, cache):
    archive = cache / f"esp-web-tools-{WEB_TOOLS_VERSION}.tgz"
    if not archive.exists():
        with urllib.request.urlopen(WEB_TOOLS_URL, timeout=45) as response:
            content = response.read()
        if base64.b64encode(hashlib.sha512(content).digest()).decode() != WEB_TOOLS_SHA512:
            raise ValueError("ESP Web Tools download checksum does not match")
        archive.write_bytes(content)
    if base64.b64encode(hashlib.sha512(archive.read_bytes()).digest()).decode() != WEB_TOOLS_SHA512:
        raise ValueError(f"ESP Web Tools checksum does not match: {archive}")

    with tempfile.TemporaryDirectory(dir=cache) as temp:
        stage = Path(temp)
        with tarfile.open(archive, "r:gz") as tar:
            license_text = tar.extractfile("package/LICENSE").read().decode("utf-8")
            for member in tar.getmembers():
                path = Path(member.name)
                if member.isfile() and str(path.parent) == "package/dist/web" and path.suffix == ".js":
                    (stage / path.name).write_bytes(tar.extractfile(member).read())
        result = subprocess.run(
            [esbuild, str(stage / "install-button.js"), "--bundle", "--format=iife",
             "--platform=browser", "--target=es2019", "--minify", "--legal-comments=inline"],
            check=True, capture_output=True, text=True,
        )
    if "</script" in result.stdout.lower():
        raise ValueError("The flasher bundle cannot be safely embedded in the HTML")
    return result.stdout, license_text


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--version", required=True, help="Firmware release version or source commit")
    cli.add_argument("--esbuild", default="esbuild", help="Path to the esbuild executable")
    args = cli.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}", args.version):
        cli.error("invalid version")

    output = ROOT / "build" / "installer"
    cache = ROOT / "build" / "installer-deps"
    output.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    parts = firmware_parts()
    bundle, license_text = build_web_tools(args.esbuild, cache)
    payload = json.dumps({"version": args.version, "parts": parts}, separators=(",", ":"))
    source = (ROOT / "installer" / "index.html").read_text(encoding="utf-8")
    page = source.replace("__FIRMWARE_VERSION__", args.version)
    page = page.replace("__INSTALLER_REVISION__", str(INSTALLER_REVISION))
    page = page.replace("__FIRMWARE_PAYLOAD__", payload).replace("__INSTALLER_BUNDLE__", bundle)
    page_name = "Install-Attitude-Indicator.html"
    (output / page_name).write_text(page, encoding="utf-8")
    archive = output / f"Attitude-Indicator-{args.version}-USB-Installer-r{INSTALLER_REVISION}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
        package.writestr(page_name, page)
        package.writestr("README.txt", (
            "航模座舱仪表 USB 安装包\n\n"
            "适用板型：Waveshare ESP32-S3-Touch-AMOLED-1.75\n"
            f"固件版本：{args.version}\n\n"
            f"安装器修订：r{INSTALLER_REVISION}\n\n"
            f"解压后在电脑 Chrome 或 Edge 中打开 {page_name}。\n"
            "用 USB 数据线连接开发板，点击「安装固件」，选择新出现的串口，\n"
            "依次点击 Install → Next → Install。安装完成后关闭浏览器标签页。\n"
            "按实际方向装好开发板，让模型保持水平静止，再重新上电，仪表会自动校准，无需按键。\n"
            "若烧录时已在桌面完成自动校准，装回座舱后可短按 PWR 重新校准，或重新上电自动校准。\n"
            "重装本项目时保持 Erase device 未勾选，即可保留安装方向和 FPS 设置。\n"
            "安装页内含固件和烧录工具，可以离线运行。\n\n"
            "固件源码：https://github.com/donne1226/attitude-indicator\n"
        ))
        package.writestr("licenses/ESP-Web-Tools.txt", (
            f"ESP Web Tools {WEB_TOOLS_VERSION}\nhttps://github.com/esphome/esp-web-tools\n\n"
            + license_text
        ))
        for name in ("HUD_FONT_LICENSE.txt", "MENU_FONT_LICENSE.txt"):
            package.write(ROOT / "firmware" / "AttitudeIndicator" / name, f"licenses/{name}")
        libraries = ("ESP32_IO_Expander/license.txt", "GFX_Library_for_Arduino/license.txt",
                     "SensorLib/LICENSE", "XPowersLib/LICENSE", "esp-lib-utils/license.txt")
        for library in libraries:
            path = Path(library)
            package.write(ROOT / "vendor" / path, f"licenses/{path.parent}.txt")
        package.writestr("licenses/firmware-sources.txt", (
            "Arduino ESP32 3.3.10: https://github.com/espressif/arduino-esp32/tree/3.3.10\n"
            "Waveshare libraries: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/"
            "tree/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/arduino/libraries\n"
        ))

    archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{archive_hash}  {archive.name}\n", encoding="ascii")
    print(f"USB installer: {archive}")
    print(f"Application SHA-256: {parts[-1]['sha256']}")
    print(f"ZIP SHA-256: {archive_hash}")


if __name__ == "__main__":
    main()
