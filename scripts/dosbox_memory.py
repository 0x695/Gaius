#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The original's state, read out of the DOSBox it runs in and written as a save file.

The original has no autosave. Its save routine, though, only copies fixed places of its memory to the file, in a fixed
order (docs/FORMATS.md, "Where every block lives at runtime", read from the routine itself), and DOSBox keeps the
emulated PC's memory in one block of its own. So a save can be taken at any moment without touching the game: find
the game in that block, copy the same places in the same order. Nothing is written to DOSBox and no key or click is
sent to it.

Checked against the game on 2026-10-11 (the US release under GOG's DOSBox 0.74-2.1): a snapshot taken while the
original's save dialog was open is the file the game then wrote, byte for byte, once the governor's name is taken
from where the routine takes it (it copies the name into the saved place only as it saves). In a running city the
saved places change in a burst of a few milliseconds once a frame (about every 57 ms) and are still in between, so
a snapshot is kept when three readings four milliseconds apart agree: it is then a state between two frames, as a
save the game writes is.

Only on Windows, and only for the US release: the places are that executable's. `python scripts/dosbox_memory.py
OUT.SAV` writes one snapshot of the running game; record_session.py takes them on a clock.
"""
import os
import re
import struct
import subprocess
import sys
import time

SAVE_SIZE = 57126

# Where the unpacked program's segments lie from its start in memory: the data segment, segment 3496 (tables and four
# of the city's layers) and segment 43A5 (the city's tiles).
DS, FAR, TILES = 0x74850, 0x34960, 0x43A50

# The 128 words the save begins with, by their address in the data segment: formats::save::kGlobalWordDsAddress
# (scripts/test_scripts.py holds the two lists together).
GLOBAL_WORDS = [
    0x6CE2, 0x6CE0, 0x6CDE, 0x6CDC, 0x6CDA, 0x6CD8, 0x6CD6, 0x6CD4, 0x6CD2, 0x6CD0, 0x6CCE, 0x6CCC,
    0x6CCA, 0x6CC8, 0x6CC6, 0x6CC4, 0x6CC2, 0x6CC0, 0x6CBE, 0x6CBC, 0x6CBA, 0x6CB6, 0x6CB4, 0x6CB2,
    0x6CB0, 0x6CAE, 0x6CAC, 0x6CAA, 0x6CA8, 0x6CA6, 0x6CA4, 0x6CA2, 0x6CA0, 0x6C9E, 0x6C9C, 0x6C7C,
    0x6C7A, 0x6C78, 0x6C72, 0x6C70, 0x6C9A, 0x6C6C, 0x6C6A, 0x6C68, 0x6C66, 0x6C64, 0x6C62, 0x6C60,
    0x6C5E, 0x6C5A, 0x6C58, 0x6C56, 0x6C54, 0x6C52, 0x6C50, 0x6C4E, 0x6C4C, 0x6C4A, 0x6C48, 0x6C46,
    0x6C44, 0x6C42, 0x6C40, 0x6BE4, 0x6BE2, 0x6BE0, 0x6C3C, 0x6C3A, 0x6C38, 0x6C36, 0x6C34, 0x6BDC,
    0x6BDA, 0x6BD8, 0x6BD6, 0x6BD4, 0x6BD2, 0x6BD0, 0x6BCE, 0x6C32, 0x6C30, 0x6C2E, 0x6C2C, 0x6C2A,
    0x6C28, 0x6C26, 0x6C24, 0x6C22, 0x6C20, 0x6C1E, 0x6C1C, 0x6C1A, 0x6C18, 0x6C16, 0x6C14, 0x6C12,
    0x6C10, 0x6C0E, 0x6C0C, 0x6C0A, 0x6C08, 0x6C06, 0x6C04, 0x6C00, 0x6BFA, 0x6BF8, 0x6BF6, 0x6BF4,
    0x6BF2, 0x6BF0, 0x6C0E, 0x6BCC, 0x6BCA, 0x6BC8, 0x6BC6, 0x6BC4, 0x6BC2, 0x6BC0, 0x6BBE, 0x6BBC,
    0x6BBA, 0x6BB8, 0x6BB6, 0x6B38, 0x6B36, 0x6B34, 0x6B32, 0x6B30,
]
# The save's last block: six words, the governor's name (12 letters), 22 words -- model's kFinalStateWords.
FINAL_HEAD = [0x6BB4, 0x6BB2, 0x6BB0, 0x6BAE, 0x6BAC, 0x6BAA]
FINAL_TAIL = [0x6CB8, 0x6C02, 0x6BFC, 0x6C98, 0x6C96, 0x6C94, 0x6C90, 0x6C8E, 0x6C8C, 0x6C8A, 0x6C88, 0x6C86, 0x6C84, 0x6C82,
              0x6C80, 0x6C7E, 0x6C5C, 0x6BEE, 0x6BEC, 0x6BEA, 0x6BE8, 0x6BE6]
NAME_PLACE = DS + 0x6B9E    # where the save routine puts the name as it saves (flat 0x04362); stale until then
NAME_POINTER = DS + 0x5858  # the far pointer it copies the name from
NAME_SIZE = 12


def layout():
    """The save, as (offset in the program's memory, size) pieces in the file's order."""
    pieces = [(DS + word, 2) for word in GLOBAL_WORDS]
    pieces += [(DS + 0x5D84, 3500), (DS + 0x5BA4, 480), (DS + 0x5B2C, 120), (DS + 0x585C, 720)]  # actors, forums, barracks, workshops
    pieces += [(TILES, 10000), (FAR + 0xC9D4, 10000), (FAR + 0xA2C4, 10000), (FAR + 0x7BB4, 10000), (FAR + 0x54A4, 10000)]
    pieces += [(FAR + 0x2752, 1602), (DS + 0x581E, 50), (DS + 0x5816, 8), (DS + 0x580C, 10)]
    pieces += [(FAR + 0x01F0, 60), (FAR + 0x022C, 60), (FAR + 0x0268, 60), (FAR + 0x02A4, 60), (FAR + 0x02E0, 72)]
    pieces += [(DS + word, 2) for word in FINAL_HEAD] + [(NAME_PLACE, NAME_SIZE)] + [(DS + word, 2) for word in FINAL_TAIL]
    return pieces


LAYOUT = layout()
NAME_IN_SAVE = SAVE_SIZE - 2 * len(FINAL_TAIL) - NAME_SIZE  # the name's place in the file
FIRST, LAST = min(o for o, _ in LAYOUT), max(o + n for o, n in LAYOUT)  # the stretch of memory a snapshot reads

# How the program is known in memory: its table of command names, and its table of construction costs (both are in
# Gaius already: construction::kCommandNames, economy::kConstructionCost).
NAMES_AT = DS + 0x1D4D
NAMES = b"No action       Main Toolbar    Clear Area      "
COSTS_AT = FAR + 0x1548
COSTS = struct.pack("<30h", 0, 0, 1, 0, 3, 3, 0, 5, 10, 5, 10, 2, 0, 20, 40, 60, 60, 200, 0, 0, 100, 200, 300, 10, 80, 25,
                    300, 20, 50, 500)
# A segment number the loader adjusts: the construction table's first entry holds its code segment, 0x11C6 in the
# file, plus the segment the program was loaded at.
SEGMENT_AT, SEGMENT_IN_FILE = 0x75ACC + 2, 0x11C6


def assemble(read):
    """The save. `read(offset, size)` gives the program's memory from `offset` of its start, or None."""
    parts = []
    for offset, size in LAYOUT:
        part = read(offset, size)
        if part is None or len(part) != size:
            return None
        parts.append(part)
    return b"".join(parts)


def assemble_from(stretch, name=None):
    """The save from the bytes of memory FIRST..LAST, with the governor's name when it is known."""
    save = assemble(lambda offset, size: stretch[offset - FIRST:offset - FIRST + size])
    if save is not None and name is not None and len(name) == NAME_SIZE:
        save = save[:NAME_IN_SAVE] + name + save[NAME_IN_SAVE + NAME_SIZE:]
    return save


def candidates(data, address):
    """Where the program would start for each place its command names are found in `data`, memory read at `address`."""
    found = []
    at = data.find(NAMES)
    while at >= 0:
        found.append(address + at - NAMES_AT)
        at = data.find(NAMES, at + 1)
    return found


def city_present(save):
    """Whether the save holds a city: before one is begun, and while one is generated, the tiles are not all there."""
    tiles = save[0x13D4:0x13D4 + 10000]
    return tiles.count(0) < 500


# --- DOSBox's memory (Windows) ----------------------------------------------------------------------------

class Dosbox:
    """The DOSBox that runs the original. snapshot() is a save or None; `problem` says why there is none."""

    PROCESS_QUERY_INFORMATION, PROCESS_VM_READ = 0x0400, 0x0010
    MEM_COMMIT, PAGE_READWRITE = 0x1000, 0x04

    def __init__(self):
        self.available = os.name == "nt"
        self.problem = "" if self.available else "only on Windows"
        self.pid = None
        self.handle = None
        self.game = None      # the program's start in DOSBox's memory
        self.next_search = 0.0
        if not self.available:
            return
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes

        class Region(ctypes.Structure):
            _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                        ("AllocationProtect", wintypes.DWORD), ("PartitionId", wintypes.WORD),
                        ("RegionSize", ctypes.c_size_t), ("State", wintypes.DWORD), ("Protect", wintypes.DWORD),
                        ("Type", wintypes.DWORD)]

        self.Region = Region
        k = self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k.OpenProcess.restype = wintypes.HANDLE
        k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        k.CloseHandle.argtypes = [wintypes.HANDLE]
        k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                        ctypes.POINTER(ctypes.c_size_t)]
        k.VirtualQueryEx.restype = ctypes.c_size_t
        k.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(Region), ctypes.c_size_t]

    @staticmethod
    def pids():
        try:
            listing = subprocess.run(["tasklist", "/FI", "IMAGENAME eq dosbox.exe", "/FO", "CSV", "/NH"],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True).stdout
        except OSError:
            return []
        return [int(m.group(1)) for m in re.finditer(r'"dosbox\.exe","(\d+)"', listing, re.I)]

    def read(self, address, size):
        buffer = self.ctypes.create_string_buffer(size)
        got = self.ctypes.c_size_t(0)
        if not self.kernel32.ReadProcessMemory(self.handle, self.ctypes.c_void_p(address), buffer, size,
                                               self.ctypes.byref(got)) or got.value != size:
            return None
        return buffer.raw

    def close(self):
        if self.handle:
            self.kernel32.CloseHandle(self.handle)
        self.handle = self.pid = self.game = None

    def is_game(self, game):
        return self.read(game + NAMES_AT, len(NAMES)) == NAMES and self.read(game + COSTS_AT, len(COSTS)) == COSTS

    def search(self):
        """Looks for the program in every DOSBox there is. True when found."""
        for pid in self.pids():
            handle = self.kernel32.OpenProcess(self.PROCESS_QUERY_INFORMATION | self.PROCESS_VM_READ, False, pid)
            if not handle:
                self.problem = "DOSBox's memory can't be read (is it run as administrator?)"
                continue
            self.handle, self.pid = handle, pid
            region, address = self.Region(), 0
            while address < 0xFFFF0000 and self.kernel32.VirtualQueryEx(handle, self.ctypes.c_void_p(address),
                                                                         self.ctypes.byref(region),
                                                                         self.ctypes.sizeof(region)):
                start, size = region.BaseAddress or 0, region.RegionSize
                if region.State == self.MEM_COMMIT and region.Protect == self.PAGE_READWRITE and size >= 0x100000:
                    data = self.read(start, size)  # the emulated PC's memory is one such block, megabytes long
                    for game in candidates(data, start) if data else []:
                        if self.is_game(game):
                            self.game, self.problem = game, ""
                            return True
                address = start + size
            self.close()
            self.problem = "DOSBox is running, but not the original's US release (or it has not started yet)"
        if not self.problem.startswith("DOSBox"):
            self.problem = "DOSBox is not running"
        return False

    def name(self):
        """The governor's name, from where the save routine takes it; None when it can't be had."""
        segment = self.read(self.game + SEGMENT_AT, 2)
        pointer = self.read(self.game + NAME_POINTER, 4)
        if segment is None or pointer is None:
            return None
        loaded_at = struct.unpack("<H", segment)[0] - SEGMENT_IN_FILE
        offset, name_segment = struct.unpack("<HH", pointer)
        if name_segment == 0:
            return None
        return self.read(self.game - loaded_at * 16 + name_segment * 16 + offset, NAME_SIZE)

    def reading(self):
        stretch = self.read(self.game + FIRST, LAST - FIRST)
        return assemble_from(stretch, self.name()) if stretch else None

    def snapshot(self, tries=12, apart=0.004):
        """(save, still): the game's state now. `still` says three readings in a row agreed, so nothing was being
        changed; after `tries` readings the last is given as it is. (None, False) when there is no game to read."""
        if not self.available:
            return None, False
        if self.game is None or not self.is_game(self.game):
            self.close()
            if time.time() < self.next_search:
                return None, False
            self.next_search = time.time() + 5.0  # a search reads all of DOSBox's memory: not at every look
            if not self.search():
                return None, False
        earlier = []
        for _ in range(tries):
            now = self.reading()
            if now is None:
                self.close()
                return None, False
            if len(earlier) >= 2 and earlier[-1] == now and earlier[-2] == now:
                return now, True
            earlier.append(now)
            time.sleep(apart)
        return earlier[-1], False


def main():
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        print(__doc__)
        return 2
    box = Dosbox()
    save, still = box.snapshot()
    if save is None:
        print("no snapshot: %s" % box.problem)
        return 1
    with open(sys.argv[1], "wb") as out:
        out.write(save)
    print("%s: the game's state%s%s" % (sys.argv[1], "" if still else " (taken while it was changing)",
                                         "" if city_present(save) else "; no city yet"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
