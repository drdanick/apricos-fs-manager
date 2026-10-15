"""End-to-end tests for the apricosfsman command line.

These drive the real binary through stdin, the way people and the OS test
suite use it, and check the disk images it writes against the filesystem
spec. The unit tests in ../ cover the details; these only check that whole
sessions produce correct images.

The binary is taken from $APRICOSFSMAN (CTest sets it to the one just
built). Run with `ctest` from the build directory, or directly:

    APRICOSFSMAN=build/src/apricosfsman pytest tests/system
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest

TRACKS, SECTORS, SECTOR_SIZE = 32, 64, 256
# Track 1 (stored as 0) holds the bitmap in sector 63 and the volume
# information in sector 64; stored sector numbers count from 0.
BITMAP_SECTOR, VOLUME_SECTOR = 62, 63
ROOT = (1, 0)


@pytest.fixture
def fsman() -> str:
    binary = os.environ.get("APRICOSFSMAN")
    if not binary:
        pytest.skip("set APRICOSFSMAN to the apricosfsman binary")
    return binary


def run(fsman: str, cwd: Path, *commands: str) -> subprocess.CompletedProcess:
    script = "\n".join(commands) + "\n"
    return subprocess.run([fsman], input=script, cwd=cwd, capture_output=True,
                          encoding="latin-1", timeout=30)


class Image:
    """Reads a disk image as the spec describes it."""

    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        assert len(self.data) == TRACKS * SECTORS * SECTOR_SIZE

    def sector(self, track: int, sector: int) -> bytes:
        start = (track * SECTORS + sector) * SECTOR_SIZE
        return self.data[start:start + SECTOR_SIZE]

    def allocated(self) -> set[tuple[int, int]]:
        bitmap = self.sector(0, BITMAP_SECTOR)
        return {(b // SECTORS, b % SECTORS) for b in range(TRACKS * SECTORS) if bitmap[b // 8] >> (b % 8) & 1}

    def entries(self, location: tuple[int, int]) -> dict[str, tuple[int, tuple[int, int]]]:
        """name -> (flags, location) for each in-use entry of a directory"""
        raw = self.sector(*location)
        result = {}
        for slot in range(32):
            entry = raw[slot * 8:slot * 8 + 8]
            if entry[0] & 0x80:
                name = entry[2:].rstrip(b"\0").decode("latin-1")
                result[name] = (entry[0] & 0xE0, (entry[0] & 0x1F, entry[1] & 0x3F))
        return result

    def file(self, metadata: tuple[int, int]) -> tuple[bytes, list[tuple[int, int]]]:
        raw = self.sector(*metadata)
        size = raw[0] | raw[1] << 8
        blocks = [(raw[2 + 2 * i], raw[3 + 2 * i] & 0x3F) for i in range(127) if raw[2 + 2 * i]]
        return b"".join(self.sector(*b) for b in blocks)[:size], blocks


def pattern(size: int, seed: int) -> bytes:
    return bytes((i * 7 + i // 256 * 13 + seed) & 0xFF for i in range(size))


def test_build_disk(fsman, tmp_path):
    """A typical session: format, make a directory, load files, mark one executable."""
    program, readme = pattern(1000, 1), pattern(300, 2)
    (tmp_path / "prog.bin").write_bytes(program)
    (tmp_path / "readme.txt").write_bytes(readme)

    result = run(fsman, tmp_path,
                 "newdisk t.dsk", "mount t.dsk", "format TESTVOL",
                 "mkdir bin", "cd bin", "touch hello", "loadfile hello prog.bin", "toggleexec hello",
                 "cd ..", "touch readme", "loadfile readme readme.txt",
                 "unmount", "exit")
    assert result.returncode == 0, result.stdout + result.stderr

    image = Image(tmp_path / "t.dsk")
    volume = image.sector(0, VOLUME_SECTOR)
    assert volume[:4] == bytes([0xC0, 0x30, 0x0C, 0x03])
    assert volume[4:12] == b"TESTVOL\0"
    assert volume[254:] == bytes([0xAA, 0x55])

    root = image.entries(ROOT)
    assert root.keys() == {"BIN", "README"}
    assert root["BIN"][0] == 0x80
    assert root["README"][0] == 0xA0

    bin_dir = image.entries(root["BIN"][1])
    assert bin_dir.keys() == {"HELLO"}
    assert bin_dir["HELLO"][0] == 0xE0

    hello, hello_blocks = image.file(bin_dir["HELLO"][1])
    readme_data, readme_blocks = image.file(root["README"][1])
    assert hello == program
    assert readme_data == readme

    # Exactly track 0 and the sectors the filesystem refers to are allocated
    used = [ROOT, root["BIN"][1], root["README"][1], bin_dir["HELLO"][1], *hello_blocks, *readme_blocks]
    assert len(set(used)) == len(used)
    assert image.allocated() == {(0, s) for s in range(SECTORS)} | set(used)


def test_files_round_trip(fsman, tmp_path):
    """Files saved back out match what was loaded, across sessions, and rm frees them."""
    sizes = {"EMPTY": 0, "SMALL": 100, "BLOCKS": 1024, "BIG": 127 * 256}
    commands = ["newdisk t.dsk", "mount t.dsk", "format TESTVOL"]
    for name, size in sizes.items():
        (tmp_path / f"{name}.in").write_bytes(pattern(size, size))
        commands += [f"touch {name}", f"loadfile {name} {name}.in"]
    result = run(fsman, tmp_path, *commands, "unmount", "exit")
    assert result.returncode == 0, result.stdout + result.stderr

    commands = ["mount t.dsk"] + [f"savefile {name} {name}.out" for name in sizes]
    result = run(fsman, tmp_path, *commands, "unmount", "exit")
    assert result.returncode == 0, result.stdout + result.stderr
    for name, size in sizes.items():
        assert (tmp_path / f"{name}.out").read_bytes() == pattern(size, size), name

    commands = ["mount t.dsk"] + [f"rm {name}" for name in sizes]
    result = run(fsman, tmp_path, *commands, "unmount", "exit")
    assert result.returncode == 0, result.stdout + result.stderr
    image = Image(tmp_path / "t.dsk")
    assert image.entries(ROOT) == {}
    assert image.allocated() == {(0, s) for s in range(SECTORS)} | {ROOT}


FILESYSTEM_COMMANDS = [
    "ls", "mkdir DIR", "cd DIR", "touch FILE", "rm FILE", "toggleexec FILE",
    "loadfile FILE in.bin", "savefile FILE out.bin",
]


@pytest.mark.parametrize("command", FILESYSTEM_COMMANDS + ["format VOL", "sync", "unmount"])
def test_commands_without_disk(fsman, tmp_path, command):
    """With no disk mounted, commands report an error instead of crashing."""
    (tmp_path / "in.bin").write_bytes(b"data")
    result = run(fsman, tmp_path, command, "exit")
    assert result.returncode == 0, f"exit status {result.returncode}\n{result.stdout}{result.stderr}"
    assert "no filesystem mounted" in result.stdout.lower()
    assert "Unknown Command" not in result.stdout
    assert "Bye!" in result.stdout


@pytest.mark.parametrize("command", FILESYSTEM_COMMANDS)
def test_commands_on_unformatted_disk(fsman, tmp_path, command):
    """With a mounted but unformatted disk, commands report an error instead of crashing."""
    (tmp_path / "in.bin").write_bytes(b"data")
    result = run(fsman, tmp_path, "newdisk t.dsk", "mount t.dsk", command, "exit")
    assert result.returncode == 0, f"exit status {result.returncode}\n{result.stdout}{result.stderr}"
    assert "not formatted" in result.stdout
    assert "Unknown Command" not in result.stdout
    assert "Bye!" in result.stdout


def test_newdisk_reports_write_failure(fsman, tmp_path):
    result = run(fsman, tmp_path, "newdisk no-such-dir/t.dsk", "exit")
    assert "written" not in result.stdout
    assert not (tmp_path / "no-such-dir").exists()
