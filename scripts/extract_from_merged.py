# scripts/extract_from_merged.py
#
# Extract OTA slot binaries from PlatformIO's merged firmware.bin using partitions.csv.
# Outputs:
#   .pio_artifacts/<env>/ota_0.bin
#   .pio_artifacts/<env>/ota_1.bin
#   .pio_artifacts/<env>/otadata.bin (optional)
#   .pio_artifacts/<env>/firmware.bin (copy)
#
# SCons post-action safe: accepts target/source/env kwargs.

import csv
from pathlib import Path
from SCons.Script import Import

Import("env")

def log(msg: str):
    print(f"[extract_from_merged] {msg}")

def parse_int_auto(s: str) -> int:
    s = (s or "").strip()
    if not s:
        raise ValueError("empty integer field")
    return int(s, 16) if s.lower().startswith("0x") else int(s, 10)

def load_partitions_csv(csv_path: Path):
    parts = []
    with csv_path.open("r", newline="", encoding="utf-8", errors="ignore") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            row = next(csv.reader([line]))
            while len(row) < 6:
                row.append("")
            name, ptype, subtype, offset, size, flags = [c.strip() for c in row[:6]]
            parts.append(
                {
                    "name": name,
                    "type": ptype,
                    "subtype": subtype,
                    "offset": parse_int_auto(offset),
                    "size": parse_int_auto(size),
                    "flags": flags,
                }
            )
    return parts

def trim_trailing_ff(data: bytes) -> bytes:
    # Most partitions are padded with 0xFF in the merged image; trim padding.
    i = len(data) - 1
    while i > 0 and data[i] == 0xFF:
        i -= 1
    return data[: i + 1]

def slice_region(blob: bytes, offset: int, size: int) -> bytes:
    end = offset + size
    if offset < 0 or size <= 0:
        raise ValueError("invalid offset/size")
    if end > len(blob):
        raise ValueError(
            f"region 0x{offset:X}..0x{end:X} exceeds blob size {len(blob)} bytes"
        )
    return blob[offset:end]

def find_partitions_csv(project_dir: Path, build_dir: Path):
    custom = env.GetProjectOption("board_build.partitions")
    candidates = []
    if custom:
        candidates.append((project_dir / custom).resolve())

    candidates += [
        (project_dir / "partitions.csv").resolve(),
        (project_dir / "partitions_custom.csv").resolve(),
        (build_dir / "partitions.csv").resolve(),
        (build_dir / "partition_table" / "partition-table.csv").resolve(),
    ]

    for p in candidates:
        if p.is_file():
            return p
    return None

def run_extraction(firmware_bin: Path):
    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    build_dir = Path(env.subst("$BUILD_DIR")).resolve()
    envname = env.subst("$PIOENV")

    if not firmware_bin.is_file():
        raise FileNotFoundError(f"firmware.bin not found at {firmware_bin}")

    partitions_csv = find_partitions_csv(project_dir, build_dir)
    if not partitions_csv:
        raise FileNotFoundError(
            "partitions.csv not found. "
            "Set board_build.partitions in platformio.ini or place partitions.csv in project root."
        )

    log(f"firmware.bin: {firmware_bin}")
    log(f"partitions:   {partitions_csv}")

    parts = load_partitions_csv(partitions_csv)

    # Grab OTA slots by name/subtype (your table uses both name=subtype)
    ota_parts = []
    for p in parts:
        if p["type"].lower() == "app" and p["subtype"].lower() in ("ota_0", "ota_1"):
            ota_parts.append(p)

    if not ota_parts:
        raise RuntimeError("No ota_0/ota_1 app partitions found in partitions.csv")

    # Also optionally export otadata (useful for OTA state)
    otadata = next(
        (p for p in parts if p["type"].lower() == "data" and p["subtype"].lower() == "ota"),
        None,
    )

    blob = firmware_bin.read_bytes()

    artifacts_dir = (project_dir / ".pio_artifacts" / envname).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    # Copy merged firmware too
    out_fw = artifacts_dir / "firmware.bin"
    if out_fw.resolve() != firmware_bin.resolve():
        out_fw.write_bytes(blob)

    # Extract each OTA slot
    for p in sorted(ota_parts, key=lambda x: x["offset"]):
        name = p["subtype"].lower()  # ota_0 / ota_1
        log(f"extract {name}: offset=0x{p['offset']:X} size=0x{p['size']:X}")
        region = slice_region(blob, p["offset"], p["size"])
        region = trim_trailing_ff(region)
        out = artifacts_dir / f"{name}.bin"
        out.write_bytes(region)
        log(f"wrote: {out} ({len(region)} bytes)")

    # Extract otadata if present
    if otadata:
        log(f"extract otadata: offset=0x{otadata['offset']:X} size=0x{otadata['size']:X}")
        region = slice_region(blob, otadata["offset"], otadata["size"])
        region = trim_trailing_ff(region)
        out = artifacts_dir / "otadata.bin"
        out.write_bytes(region)
        log(f"wrote: {out} ({len(region)} bytes)")

    log(f"artifacts dir: {artifacts_dir}")

# SCons post-action signature: must accept keyword args
def main(target=None, source=None, env=None, **kwargs):
    # If attached to firmware.bin, target[0] is that file node.
    if target and len(target) > 0:
        firmware_bin = Path(str(target[0])).resolve()
    else:
        firmware_bin = Path(env.subst("$BUILD_DIR")).resolve() / "firmware.bin"

    run_extraction(firmware_bin)

# Attach to the actual produced merged binary (most reliable)
env.AddPostAction("$BUILD_DIR/firmware.bin", main)
