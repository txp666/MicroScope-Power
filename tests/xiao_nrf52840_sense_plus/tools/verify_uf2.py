#!/usr/bin/env python3
"""Verify the XIAO combined UF2 against every Intel HEX data byte (stdlib only)."""

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path


FAMILY_ID = 0xADA52840
APP_START = 0x27000
APP_END = 0xED000  # Exclusive: preserve the bootloader and its settings.
RAM_START = 0x20000000
RAM_END = 0x20040000  # nRF52840 SRAM end, exclusive for data, valid initial SP.
SKETCH = "xiao_nrf52840_combined_test"
FRIENDLY_UF2 = "xiao-sense-plus-combined-test.uf2"
FQBN = "Seeeduino:nrf52:xiaonRF52840SensePlus"
TEST_ROOT = Path("tests") / "xiao_nrf52840_sense_plus"


class ValidationError(ValueError):
    """A file or build invariant failed."""


def require(condition, message):
    if not condition:
        raise ValidationError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def source_hashes(repo_root):
    test_root = repo_root / TEST_ROOT
    sketch_dir = test_root / "firmware" / SKETCH
    require((sketch_dir / (SKETCH + ".ino")).is_file(),
            "Missing combined sketch entry point")
    paths = sorted(path for path in sketch_dir.rglob("*")
                   if path.is_file() and path.suffix.lower() in {".h", ".cpp", ".ino"})
    paths.extend(test_root / "tools" / name for name in ("build.sh", "verify_uf2.py"))
    return {path.relative_to(repo_root).as_posix(): sha256(path.read_bytes())
            for path in paths}


def parse_hex(text):
    """Read Intel HEX data, validating checksums and segment/linear addressing."""
    memory = {}
    base = 0
    eof = False
    start_records = []
    for line_number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        require(not eof, "HEX content follows EOF at line %d" % line_number)
        require(line.startswith(":"), "Invalid HEX prefix at line %d" % line_number)
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as error:
            raise ValidationError("Invalid HEX encoding at line %d" % line_number) from error
        require(len(record) >= 5, "Short HEX record at line %d" % line_number)
        count, high, low, record_type = record[:4]
        address = (high << 8) | low
        payload = record[4:-1]
        require(len(record) == count + 5,
                "HEX byte count mismatch at line %d" % line_number)
        require(sum(record) & 0xFF == 0,
                "HEX checksum mismatch at line %d" % line_number)
        if record_type == 0:
            require(address + count <= 0x10000,
                    "HEX data crosses a 64 KiB record boundary at line %d" % line_number)
            start = base + address
            require(APP_START <= start and start + count <= APP_END,
                    "HEX data outside application region at line %d" % line_number)
            for offset, value in enumerate(payload):
                target = start + offset
                require(target not in memory, "Overlapping HEX data at 0x%x" % target)
                memory[target] = value
        elif record_type == 1:
            require(count == 0 and address == 0, "Invalid HEX EOF record")
            eof = True
        elif record_type in (2, 4):
            require(count == 2 and address == 0, "Invalid HEX address extension")
            base = int.from_bytes(payload, "big") << (4 if record_type == 2 else 16)
        elif record_type in (3, 5):
            require(count == 4 and address == 0, "Invalid HEX start-address record")
            if record_type == 3:
                start_address = (int.from_bytes(payload[:2], "big") << 4) + int.from_bytes(payload[2:], "big")
            else:
                start_address = int.from_bytes(payload, "big")
            start_records.append({"record_type": record_type, "address": hex(start_address)})
        else:
            raise ValidationError("Unsupported HEX record type %d" % record_type)
    require(eof, "HEX is missing its EOF record")
    require(bool(memory), "HEX contains no application data")
    require(min(memory) == APP_START, "HEX does not start at the application vector table")
    return memory, start_records


def parse_uf2(data):
    require(bool(data) and len(data) % 512 == 0,
            "UF2 must contain a nonzero whole number of 512-byte blocks")
    block_count = len(data) // 512
    memory = {}
    seen_blocks = set()
    payload_sizes = set()
    for file_index in range(block_count):
        block = data[file_index * 512:(file_index + 1) * 512]
        magic0, magic1, flags, target, size, number, total, family = struct.unpack_from("<8I", block)
        require(magic0 == 0x0A324655 and magic1 == 0x9E5D5157,
                "Invalid UF2 start magic in block %d" % file_index)
        require(struct.unpack_from("<I", block, 508)[0] == 0x0AB16F30,
                "Invalid UF2 end magic in block %d" % file_index)
        require(flags == 0x2000, "UF2 has unsupported flags in block %d" % file_index)
        require(family == FAMILY_ID, "UF2 family is not nRF52840 in block %d" % file_index)
        require(total == block_count, "UF2 declared block count is inconsistent")
        require(0 <= number < block_count and number not in seen_blocks,
                "UF2 has an invalid or duplicate block number")
        require(0 < size <= 476 and size % 4 == 0,
                "UF2 has an invalid payload size")
        require(target % 4 == 0, "UF2 target is not word-aligned")
        require(APP_START <= target and target + size <= APP_END,
                "UF2 block overlaps SoftDevice/bootloader or lies outside application flash")
        require(not any(block[32 + size:508]), "UF2 reserved block bytes are not zero")
        seen_blocks.add(number)
        payload_sizes.add(size)
        for offset, value in enumerate(block[32:32 + size]):
            address = target + offset
            require(address not in memory, "Overlapping UF2 payloads at 0x%x" % address)
            memory[address] = value
    require(seen_blocks == set(range(block_count)), "UF2 is missing block numbers")
    require(min(memory) == APP_START, "UF2 does not start at the application vector table")
    return memory, {"bytes": len(data), "blocks": block_count,
                    "family": hex(FAMILY_ID), "flags": "0x2000",
                    "payload_sizes": sorted(payload_sizes),
                    "first_address": hex(min(memory)),
                    "end_address_exclusive": hex(max(memory) + 1)}


def verify_vectors(memory, hex_memory):
    require(all(APP_START + offset in hex_memory for offset in range(8)),
            "HEX is missing the initial stack pointer or reset vector")
    words = bytes(memory[APP_START + offset] for offset in range(8))
    initial_sp, reset = struct.unpack("<2I", words)
    require(RAM_START < initial_sp <= RAM_END and initial_sp % 8 == 0,
            "Initial stack pointer is outside/aligned incorrectly for nRF52840 SRAM")
    require(reset & 1 == 1, "Reset vector does not select Thumb state")
    reset_target = reset & ~1
    require(APP_START <= reset_target < APP_END and reset_target in hex_memory,
            "Reset vector does not point into HEX application code")
    return {"initial_sp": hex(initial_sp), "reset": hex(reset),
            "reset_target": hex(reset_target)}


def verify(repo_root, output_dir, manifest):
    build_dir = repo_root / TEST_ROOT / ".build"
    compile_dir = build_dir / "arduino"
    canonical = compile_dir / (SKETCH + ".ino.uf2")
    friendly = output_dir / FRIENDLY_UF2
    hex_path = compile_dir / (SKETCH + ".ino.hex")
    data = canonical.read_bytes()
    require(friendly.read_bytes() == data, "Friendly UF2 copy differs from compiler output")
    hex_data = hex_path.read_bytes()
    memory, uf2_info = parse_uf2(data)
    hex_memory, start_records = parse_hex(hex_data.decode("ascii"))
    for address, value in hex_memory.items():
        require(memory.get(address) == value, "HEX/UF2 mismatch at 0x%x" % address)
    padding_count = 0
    for address, value in memory.items():
        if address not in hex_memory:
            require(value == 0, "UF2 non-HEX byte is not zero padding at 0x%x" % address)
            padding_count += 1
    vectors = verify_vectors(memory, hex_memory)
    sources = source_hashes(repo_root)
    if manifest is not None:
        expected = json.loads(manifest.read_text(encoding="utf-8"))
        require(sources == expected, "Source files changed after the pre-build fingerprint was captured")

    toolchain = {"Seeeduino_nrf52_required": "1.1.13"}
    version_file = build_dir / "arduino-cli-version.txt"
    if version_file.is_file():
        toolchain["arduino_cli_version_output"] = version_file.read_text(encoding="utf-8").strip()
    core_file = build_dir / "core-list.json"
    if core_file.is_file():
        platforms = json.loads(core_file.read_text(encoding="utf-8")).get("platforms", [])
        installed = next((p.get("installed_version", p.get("installed"))
                          for p in platforms if p.get("id") == "Seeeduino:nrf52"), None)
        require(installed == "1.1.13", "Recorded core version is not Seeeduino:nrf52 1.1.13")
        toolchain["Seeeduino_nrf52_installed"] = installed

    build = {"source_fingerprints_match_pre_build": manifest is not None}
    log = output_dir / "build.log"
    log_data = log.read_bytes()
    require(log_data == (build_dir / "build.log").read_bytes(),
            "Delivered build.log differs from the compiler log")
    log_text = log_data.decode("utf-8", errors="replace")
    build["log_file"] = (build_dir / "build.log").relative_to(repo_root).as_posix()
    build["delivered_log_file"] = log.name
    build["log_sha256"] = sha256(log_data)
    for name, pattern in (
            ("flash_bytes", r"Sketch uses ([\d,]+) bytes"),
            ("ram_bytes", r"Global variables use ([\d,]+) bytes")):
        match = re.search(pattern, log_text)
        if match:
            build[name] = int(match.group(1).replace(",", ""))

    uf2_info.update({"file": friendly.name,
                     "canonical_file": canonical.relative_to(repo_root).as_posix(),
                     "sha256": sha256(data), "friendly_copy_identical": True,
                     "zero_padding_bytes": padding_count})
    return {"status": "passed", "board_fqbn": FQBN,
            "validation_scope": "UF2 structure/family/application bounds, every HEX data byte, zero padding, vectors, copy identity and source fingerprints; no hardware flash or current measurement.",
            "application_region": {"start": hex(APP_START), "end_exclusive": hex(APP_END)},
            "toolchain": toolchain, "build": build, "uf2": uf2_info,
            "hex": {"file": hex_path.relative_to(repo_root).as_posix(), "sha256": sha256(hex_data),
                    "data_bytes": len(hex_memory), "start_records": start_records,
                    "every_data_byte_matches_uf2": True},
            "vectors": vectors, "sources_sha256": sources}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--source-manifest", type=Path,
                        help="Require exact match to source hashes captured before compiling")
    parser.add_argument("--snapshot-sources", type=Path,
                        help="Write source hashes before compiling, then exit")
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    output_dir = (args.output_dir or repo_root / TEST_ROOT / "artifacts").resolve()
    try:
        if args.snapshot_sources:
            args.snapshot_sources.write_text(json.dumps(source_hashes(repo_root), indent=2) + "\n", encoding="utf-8")
            return 0
        report = verify(repo_root, output_dir, args.source_manifest)
        validation = output_dir / "validation.json"
        validation.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        names = [FRIENDLY_UF2, "validation.json", "build.log"]
        checksums = [sha256((output_dir / name).read_bytes()) + "  " + name for name in names]
        (output_dir / "SHA256SUMS").write_text("\n".join(checksums) + "\n", encoding="ascii")
        print("Verified %s: %d blocks, every HEX byte matches, source fingerprints recorded." %
              (FRIENDLY_UF2, report["uf2"]["blocks"]))
        return 0
    except (ValidationError, OSError, UnicodeError, json.JSONDecodeError) as error:
        if output_dir.is_dir() and not args.snapshot_sources:
            (output_dir / "validation.json").write_text(
                json.dumps({"status": "failed", "error": str(error)}, indent=2) + "\n", encoding="utf-8")
            (output_dir / "SHA256SUMS").unlink(missing_ok=True)
        print("UF2 verification failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
