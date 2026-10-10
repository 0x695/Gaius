#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record a play session: every save either game writes, and what was on screen.

    record_session.py [OUT] [--original FOLDER] [--every SECONDS] [--play-gaius] [--video]

Start it, then play -- the original Caesar in its DOSBox, Gaius, or one after the
other -- and stop it with Ctrl+C. While it runs:

  * Every save written is copied into OUT/<day>/original/saves or .../gaius/saves,
    named by the time of day (213045_CAESARXX.SAV). A slot saved over ten
    times is ten files, in order: nothing is lost to the game's few save names.
    Gaius's quick, automatic and away saves count too.
  * The original's screen: DOSBox's own screenshot key (Ctrl+F5) is pressed for
    you every few seconds while DOSBox is the window in front, and just after each
    save. Those pictures are the game's frame exactly, not a photograph of the
    window, and are moved to .../original/screens. (Windows only.)
  * Gaius's screen: started with --capture-dir (this script's --play-gaius does
    it), Gaius writes its own picture every few seconds; they are moved to
    .../gaius/screens.
  * OUT/<day>/session.csv lists each save with the time and, when the tools are
    built, the game date, the people and the funds.
  * With --video, OBS Studio films the two games' windows for the whole session
    into OUT/<day>/video (Windows; see "A video of the session" below).

The saves and pictures are the original game's property or show its art: OUT may
not be a folder git would commit (the default, export/sessions, is ignored).

Saves made this way are what scripts/check_saves.py and tools/month_check want:
two saves a few seconds apart pin down what the simulation did in between."""

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from _gaius import REPO, ScriptError, main_wrapper, output_dir, save_summary, tool

SAVE_SIZE = 57126  # formats::save::kSaveSize: a save is always this long


def gaius_data_folder():
    """Where Gaius keeps the player's files (platform/paths.cpp)."""
    if os.name == "nt":
        base = os.environ.get("APPDATA")
        return Path(base) / "Gaius" if base else None
    base = os.environ.get("XDG_DATA_HOME") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return Path(base) / "gaius"


def original_folder(argument=None):
    """The original's install: the argument, $GAIUS_ORIGINAL, or where GOG puts it. None when there is none."""
    candidates = [argument, os.environ.get("GAIUS_ORIGINAL"), r"C:\Program Files (x86)\Caesar", r"C:\GOG Games\Caesar"]
    for candidate in candidates:
        if candidate and Path(candidate).is_dir():
            return Path(candidate)
    return None


def original_save_folders(install):
    """Where the original's saves land. GOG's DOSBox mounts cloud_saves over the game folder, so a save written to
    C:\\US is the file cloud_saves/US/NAME.SAV; without that overlay it is in the game folder itself."""
    return [install / "cloud_saves" / "US", install / "cloud_saves", install / "US", install]


def time_name(moment, name, folder):
    """HHMMSS_NAME in folder, with _2, _3 ... when that second already has one."""
    stem, suffix = os.path.splitext(name)
    candidate = folder / ("%s_%s" % (moment.strftime("%H%M%S"), name))
    n = 2
    while candidate.exists():
        candidate = folder / ("%s_%s_%d%s" % (moment.strftime("%H%M%S"), stem, n, suffix))
        n += 1
    return candidate


class SaveWatcher:
    """Copies each save written in some folders to an archive folder, named by the time it was written.

    A save is taken once it is whole (SAVE_SIZE bytes) and has not changed between two looks, so one caught while the
    game is still writing it waits for the next look."""

    def __init__(self, folders, archive):
        self.folders = [Path(f) for f in folders]
        self.archive = Path(archive)
        self.known = {}    # path -> (mtime_ns, size): as it was at the start, or when it was last copied
        self.pending = {}  # path -> (mtime_ns, size): seen changed, to be confirmed by the next look
        self.known = self.look()

    def look(self):
        found = {}
        for folder in self.folders:
            try:
                entries = list(os.scandir(str(folder)))
            except OSError:
                continue  # not there (yet): cloud_saves/US appears with the first save
            for entry in entries:
                if not entry.name.upper().endswith(".SAV"):
                    continue
                try:
                    status = entry.stat()
                except OSError:
                    continue
                found[Path(entry.path)] = (status.st_mtime_ns, status.st_size)
        return found

    def poll(self):
        """The saves copied by this look: a list of (source, copy)."""
        copied = []
        for path, stamp in self.look().items():
            if self.known.get(path) == stamp:
                self.pending.pop(path, None)
                continue
            if stamp[1] != SAVE_SIZE or self.pending.get(path) != stamp:
                self.pending[path] = stamp
                continue
            self.archive.mkdir(parents=True, exist_ok=True)
            # Named by when it was seen, a second or so after it was written: the file's own time is the game's to set.
            target = time_name(datetime.now(), path.name.upper(), self.archive)
            try:
                shutil.copy2(str(path), str(target))
            except OSError:
                continue  # the game has it open: the next look
            self.known[path] = stamp
            del self.pending[path]
            copied.append((path, target))
        return copied


def dosbox_capture_folders(install):
    """Where DOSBox puts its screenshots: "capture" beside its configuration file (GOG keeps that in the install
    folder), or beside DOSBox itself when it is started without one."""
    return [install / "capture", install / "DOSBOX" / "capture"]


class PictureMover:
    """Moves the pictures a game writes into its folders (DOSBox's capture folder, Gaius's --capture-dir) to the
    session's, named by the time each was written. A file is left alone until it is half a second old, so none is
    taken half written."""

    def __init__(self, sources, target):
        self.sources = [Path(s) for s in sources]
        self.target = Path(target)
        self.moved = 0

    def poll(self):
        entries = []
        for source in self.sources:
            try:
                entries += list(os.scandir(str(source)))
            except OSError:
                continue  # no pictures there yet
        now = time.time()
        moved = 0
        for entry in sorted(entries, key=lambda e: e.name):
            if not entry.name.lower().endswith(".png"):
                continue
            try:
                status = entry.stat()
                if status.st_size == 0 or now - status.st_mtime < 0.5:
                    continue  # empty, or just written: not finished
                self.target.mkdir(parents=True, exist_ok=True)
                target = time_name(datetime.fromtimestamp(status.st_mtime), "screen.png", self.target)
                shutil.copy2(entry.path, str(target))
            except OSError:
                continue
            try:
                os.unlink(entry.path)
            except OSError:
                os.unlink(str(target))  # the game still has it open: taken at a later look, and only once
                continue
            moved += 1
        self.moved += moved
        return moved


# --- DOSBox's screenshot key (Windows) --------------------------------------------------------------

class DosboxKey:
    """Presses Ctrl+F5, DOSBox's "save a screenshot", when DOSBox is the window in front -- and only then, so the keys
    can never reach another program. It waits while the player holds any key: Ctrl with the key they are pressing
    would be another of DOSBox's commands (with Alt, its video capture; with F9, closing it).

    And it waits until DOSBox is running a DOS program, which its window title says ("... Program:      CSR"; while
    it starts, the title is "DOSBox" and then "... Program:   DOSBOX"). DOSBox 0.74 must not be asked for a screenshot
    before it has drawn its first frame: RENDER_Init leaves render.updating set with no screen size yet, so the first
    RENDER_EndUpdate passes a 0 x 0 picture to libpng, whose error ends the program (seen 2026-10-09: an empty
    csr_000.png, "libpng error: Image width or height is zero in IHDR", and DOSBox gone). A program is only run once
    the machine has been drawing for a while, and the state cannot come back."""

    VK_CONTROL, VK_F5 = 0x11, 0x74
    SETTLE = 3.0  # seconds the game must have been seen running, in front, before the first press
    KEYEVENTF_KEYUP = 0x0002
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

    def __init__(self):
        self.available = os.name == "nt"
        self.running_since = None  # when the game was first seen running in the DOSBox in front
        if not self.available:
            return
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.wintypes = wintypes
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.user32.GetForegroundWindow.restype = wintypes.HWND
        self.kernel32.OpenProcess.restype = wintypes.HANDLE
        self.kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                             ctypes.POINTER(wintypes.DWORD)]
        self.kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    def front_program(self):
        """The file name of the program whose window is in front, lower case; "" when it can't be told (no window, or
        a program run as administrator, which this script could not send keys to either)."""
        ctypes, wintypes = self.ctypes, self.wintypes
        window = self.user32.GetForegroundWindow()
        if not window:
            return ""
        pid = wintypes.DWORD(0)
        self.user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
        handle = self.kernel32.OpenProcess(self.PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
        if not handle:
            return ""
        try:
            buffer = ctypes.create_unicode_buffer(1024)
            size = wintypes.DWORD(len(buffer))
            if not self.kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
                return ""
            return os.path.basename(buffer.value).lower()
        finally:
            self.kernel32.CloseHandle(handle)

    def dosbox_in_front(self):
        return self.available and self.front_program().startswith("dosbox")

    def front_title(self):
        title = self.ctypes.create_unicode_buffer(512)
        self.user32.GetWindowTextW(self.user32.GetForegroundWindow(), title, 512)
        return title.value

    @staticmethod
    def program_in_title(title):
        """The DOS program a DOSBox title names ("CSR"), or "" when it names none or DOSBox's own shell."""
        found = re.search(r"Program:\s*(\S+)\s*$", title)
        return "" if not found or found.group(1).upper() == "DOSBOX" else found.group(1)

    def game_running(self):
        """Whether the DOSBox in front has been running a DOS program for SETTLE seconds: only then may it be asked
        for a screenshot (the class comment says why)."""
        if not self.dosbox_in_front() or not self.program_in_title(self.front_title()):
            self.running_since = None
            return False
        if self.running_since is None:
            self.running_since = time.time()
        return time.time() - self.running_since >= self.SETTLE

    def held(self, key):
        return (self.user32.GetAsyncKeyState(key) & 0x8000) != 0

    def press(self):
        """Presses the key. False when DOSBox is not in front with the game running, or the player holds a key
        (mouse buttons don't count: a road is dragged with one held)."""
        if not self.game_running():
            return False
        if any(self.held(k) for k in range(0x08, 0xFF)):
            return False
        # With scan codes: DOSBox reads those, not the virtual keys.
        control = self.user32.MapVirtualKeyW(self.VK_CONTROL, 0)
        f5 = self.user32.MapVirtualKeyW(self.VK_F5, 0)
        self.user32.keybd_event(self.VK_CONTROL, control, 0, 0)
        time.sleep(0.02)
        self.user32.keybd_event(self.VK_F5, f5, 0, 0)
        time.sleep(0.03)
        self.user32.keybd_event(self.VK_F5, f5, self.KEYEVENTF_KEYUP, 0)
        time.sleep(0.01)
        self.user32.keybd_event(self.VK_CONTROL, control, self.KEYEVENTF_KEYUP, 0)
        return True


# --- A video of the session: OBS Studio (Windows) -------------------------------------------------------
#
# With --video the script runs OBS Studio for the length of the session, on a profile and a scene collection of its
# own (OBS_NAME, written again at each start, so nothing set by hand in them lasts): the two games' windows -- only
# those, not the desktop -- each fitted to a 1920 x 1080 picture, 30 frames a second, with the sound the PC plays and
# no microphone, as .mkv (which survives a crash) in the session's video folder. OBS is started already recording,
# stopped with a key no keyboard has (F23, its Stop Recording hotkey in that profile) and then closed.

OBS_NAME = "Gaius-recorder"
VK_F23, VK_F24 = 0x86, 0x87  # F24 is the profile's Screenshot Output hotkey: a still of what is being filmed


def obs_program(argument=None):
    """obs64.exe: the argument, $GAIUS_OBS, or where its installer puts it. None when it isn't there."""
    candidates = [argument, os.environ.get("GAIUS_OBS")]
    for base in (os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)")):
        if base:
            candidates.append(os.path.join(base, "obs-studio", "bin", "64bit", "obs64.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return None


def obs_profile(video_folder):
    """The profile's basic.ini: where and how OBS records."""
    return "\n".join([
        "[General]", "Name=" + OBS_NAME, "",
        "[Output]", "Mode=Simple", "FilenameFormatting=%CCYY-%MM-%DD %hh-%mm-%ss", "",
        "[SimpleOutput]", "FilePath=" + Path(video_folder).as_posix(), "RecFormat2=mkv", "RecQuality=Small",
        "RecEncoder=x264", "RecTracks=1", "",
        "[Video]", "BaseCX=1920", "BaseCY=1080", "OutputCX=1920", "OutputCY=1080", "FPSType=0", "FPSCommon=30", "",
        "[Hotkeys]", 'OBSBasic.StopRecording={"bindings":[{"key":"OBS_KEY_F23"}]}',
        'OBSBasic.Screenshot={"bindings":[{"key":"OBS_KEY_F24"}]}', ""])


def obs_scenes():
    """The scene collection: a window capture for each game, fitted to the picture, and the PC's sound. A window is
    found by its program (priority 2), since DOSBox's title changes as it runs; method 2 is Windows' own capture,
    which sees a window drawn by the graphics card (Gaius's) and one that covers the screen (DOSBox's)."""
    import json

    def window(name, spec):
        return {"id": "window_capture", "versioned_id": "window_capture", "name": name, "mixers": 0,
                "settings": {"window": spec, "priority": 2, "method": 2, "cursor": True, "client_area": True}}

    def item(name, number):
        return {"name": name, "id": number, "visible": True, "locked": True, "pos": {"x": 0.0, "y": 0.0}, "align": 5,
                "bounds_type": 2, "bounds_align": 0, "bounds": {"x": 1920.0, "y": 1080.0}}

    names = ["Caesar (DOSBox)", "Gaius"]
    return json.dumps({
        "name": OBS_NAME, "current_scene": "Games", "current_program_scene": "Games",
        "scene_order": [{"name": "Games"}],
        "DesktopAudioDevice1": {"id": "wasapi_output_capture", "versioned_id": "wasapi_output_capture",
                                "name": "Desktop Audio", "mixers": 255, "volume": 1.0, "muted": False,
                                "settings": {"device_id": "default"}},
        "sources": [
            window(names[0], "DOSBox:SDL_app:DOSBox.exe"),
            window(names[1], "Gaius:SDL_app:gaius_viewer.exe"),
            {"id": "scene", "versioned_id": "scene", "name": "Games", "mixers": 0,
             "settings": {"id_counter": 2, "items": [item(names[0], 1), item(names[1], 2)]}},
        ]}, indent=2)


class Obs:
    """OBS Studio run for one session. start() returns False, with the reason printed, when there is no video."""

    def __init__(self, program, video_folder):
        self.program = Path(program)
        self.folder = Path(video_folder)
        self.process = None

    @staticmethod
    def running(program="obs64.exe"):
        try:
            listing = subprocess.run(["tasklist", "/FI", "IMAGENAME eq " + program, "/NH"], stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, universal_newlines=True).stdout
        except OSError:
            return False
        return program in listing.lower()

    def start(self):
        if os.name != "nt" or not os.environ.get("APPDATA"):
            print("video: only on Windows; no video")
            return False
        if self.running():
            print("video: OBS is already running. Close it and start again for a video of this session")
            return False
        config = Path(os.environ["APPDATA"]) / "obs-studio"
        (config / "basic" / "profiles" / OBS_NAME).mkdir(parents=True, exist_ok=True)
        (config / "basic" / "scenes").mkdir(parents=True, exist_ok=True)
        self.folder.mkdir(parents=True, exist_ok=True)
        (config / "basic" / "profiles" / OBS_NAME / "basic.ini").write_text(obs_profile(self.folder), encoding="utf-8")
        (config / "basic" / "scenes" / (OBS_NAME + ".json")).write_text(obs_scenes(), encoding="utf-8")
        for name in ("global.ini", "user.ini"):  # a first start of OBS: no set-up wizard over the game
            if not (config / name).exists():
                (config / name).write_text("[General]\nFirstRun=true\n", encoding="utf-8")
        # From its own folder: OBS finds its files from there.
        before = set(self.folder.glob("*.mkv"))
        self.process = subprocess.Popen(
            [str(self.program), "--profile", OBS_NAME, "--collection", OBS_NAME, "--startrecording",
             "--minimize-to-tray", "--disable-shutdown-check", "--disable-updater"], cwd=str(self.program.parent))
        print("video: starting OBS (it takes some seconds) ...")
        for _ in range(180):  # its file appears when it records
            if set(self.folder.glob("*.mkv")) - before:
                print("video: OBS is filming the games' windows into %s" % self.folder)
                return True
            if self.process.poll() is not None:
                break
            time.sleep(0.5)
        print("video: OBS did not start recording; look at its window (it is in the tray)")
        return self.process.poll() is None

    def newest(self):
        files = [p for p in self.folder.glob("*.mkv")]
        return max(files, key=lambda p: p.stat().st_mtime) if files else None

    def press(self, key):
        import ctypes
        user32 = ctypes.WinDLL("user32")
        user32.keybd_event(key, 0, 0, 0)
        time.sleep(0.15)  # OBS looks at the keyboard every few hundredths of a second
        user32.keybd_event(key, 0, 2, 0)

    def sees(self):
        """Whether OBS has a picture of the game in front: it is asked for a still of what it films (its Screenshot
        Output hotkey), and a still of nothing is a small file. None when no still came."""
        before = set(self.folder.glob("Screenshot*.png"))
        self.press(VK_F24)
        for _ in range(10):
            time.sleep(0.5)
            stills = set(self.folder.glob("Screenshot*.png")) - before
            if stills:
                time.sleep(0.3)  # until it is written
                return max(p.stat().st_size for p in stills) > 20000  # a black 1920 x 1080 PNG is 6 kB
        return None

    def stop(self):
        """Stops the recording, waits for the file to be finished, and closes OBS."""
        if self.process is None or self.process.poll() is not None:
            return
        self.press(VK_F23)
        film = self.newest()
        for _ in range(40):  # OBS writes the file through a helper program, which ends when the file is whole
            time.sleep(0.5)
            if not self.running("obs-ffmpeg-mux.exe"):
                break
        import ctypes
        from ctypes import wintypes
        user32 = ctypes.WinDLL("user32")
        pid = self.process.pid

        def close(window, _):
            owner = wintypes.DWORD(0)
            user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(window, title, 256)
            if owner.value == pid and title.value.startswith("OBS "):
                user32.PostMessageW(window, 0x0010, 0, 0)  # WM_CLOSE
            return True

        callback = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)(close)
        user32.EnumWindows(callback, 0)
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            print("video: OBS is still open (it asks before closing while it records); close it from its window")
        if film:
            print("video: %s, %.0f MB" % (film, film.stat().st_size / 1e6))


# --- The session ---------------------------------------------------------------------------------------

def describe(path):
    """"year -12, month 3, 1240 people, 5310 Dn" for a save, or "" when the tools aren't built or it won't read."""
    try:
        facts = save_summary(path)
        return "year %d, month %d, %d people, %d Dn" % (facts["year"], facts["month"] + 1, facts["population"],
                                                        facts["funds"])
    except (ScriptError, KeyError, TypeError, OSError):
        return ""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", nargs="?", default=str(REPO / "export" / "sessions"),
                        help="where sessions are kept, a folder a day (default: export/sessions)")
    parser.add_argument("--original", help="the original's install folder (default: $GAIUS_ORIGINAL, or GOG's "
                                           r"C:\Program Files (x86)\Caesar)")
    parser.add_argument("--dosbox-captures", help="DOSBox's capture folder (default: capture in the original's "
                                                  "folder, where GOG's DOSBox writes)")
    parser.add_argument("--gaius-saves", help="Gaius's saves folder (default: its own)")
    parser.add_argument("--gaius-captures", help="where Gaius is told to write its pictures (default: beside its saves)")
    parser.add_argument("--every", type=float, default=5.0,
                        help="seconds between pictures of each game; 0 for the original: only after a save (default: 5)")
    parser.add_argument("--play-gaius", action="store_true", help="start Gaius now, writing its pictures")
    parser.add_argument("--gaius-exe", help="the Gaius to start (default: the built gaius_viewer)")
    parser.add_argument("--video", action="store_true",
                        help="also film the two games' windows with OBS Studio, into the session's video folder")
    parser.add_argument("--obs-exe", help="OBS Studio's obs64.exe (default: $GAIUS_OBS, or where it installs)")
    options = parser.parse_args()

    started = datetime.now()
    session = output_dir(Path(options.out) / started.strftime("%Y-%m-%d"))
    data = gaius_data_folder()
    gaius_saves = Path(options.gaius_saves) if options.gaius_saves else (data / "saves" if data else None)
    gaius_captures = Path(options.gaius_captures) if options.gaius_captures else (data / "capture" if data else None)
    install = original_folder(options.original)
    if options.original and install is None:
        raise ScriptError("%s: no such folder" % options.original)

    watchers = {}
    movers = {}
    if install is not None:
        watchers["original"] = SaveWatcher(original_save_folders(install), session / "original" / "saves")
        captures = [Path(options.dosbox_captures)] if options.dosbox_captures else dosbox_capture_folders(install)
        movers["original"] = PictureMover(captures, session / "original" / "screens")
        print("original: saves under %s, DOSBox's pictures from %s" % (install, captures[0]))
    else:
        print("original: not found (pass --original FOLDER); only Gaius is recorded")
    if gaius_saves is not None and gaius_captures is not None:
        watchers["gaius"] = SaveWatcher([gaius_saves], session / "gaius" / "saves")
        movers["gaius"] = PictureMover([gaius_captures], session / "gaius" / "screens")
        print("gaius: saves in %s, pictures in %s" % (gaius_saves, gaius_captures))
    if not watchers:
        raise ScriptError("nothing to record: no original found and no Gaius folder (pass --original, --gaius-saves "
                          "and --gaius-captures)")
    print("session: %s" % session)

    key = DosboxKey()
    if install is not None and not key.available:
        print("original: no pictures on this system (DOSBox's key is only pressed on Windows; press Ctrl+F5 yourself)")

    video = None
    if options.video:
        program = obs_program(options.obs_exe)
        if program is None:
            raise ScriptError("--video needs OBS Studio (obsproject.com); pass --obs-exe if it is installed elsewhere")
        video = Obs(program, session / "video")
        if not video.start():
            video = None

    game = None
    if options.play_gaius:
        if "gaius" not in watchers:
            raise ScriptError("--play-gaius: no Gaius folder to record (pass --gaius-saves and --gaius-captures)")
        exe = Path(options.gaius_exe) if options.gaius_exe else tool("gaius_viewer")
        game = subprocess.Popen([str(exe), "--capture-dir", str(gaius_captures), "--capture-every",
                                 str(options.every if options.every > 0 else 5.0)])
        print("gaius: started %s" % exe)

    log_path = session / "session.csv"
    new_log = not log_path.exists() or log_path.stat().st_size == 0
    counts = {name: 0 for name in watchers}
    next_picture = 0.0   # when the original's next timed picture is due
    after_save = []      # times at which a picture follows a save of the original's
    in_front_since = {}  # --video: when each game's window came to the front, until OBS's picture of it is checked
    checked = set()
    print("recording; Ctrl+C stops it")
    try:
        with open(str(log_path), "a", newline="", encoding="utf-8") as log_file:
            log = csv.writer(log_file)
            if new_log:
                log.writerow(["time", "game", "save", "copy", "what"])
                log_file.flush()
            while True:
                now = time.time()
                for name, watcher in watchers.items():
                    for source, copy in watcher.poll():
                        counts[name] += 1
                        what = describe(copy)
                        stamp = datetime.now().strftime("%H:%M:%S")
                        print("%s  %-8s %-13s -> %s%s" % (stamp, name, source.name, copy.name, "  (" + what + ")" if what else ""))
                        log.writerow([stamp, name, source.name, copy.name, what])
                        log_file.flush()
                        if name == "original":
                            after_save += [now + 1.5, now + 5.0]  # the save dialog, then the city again
                # The original's pictures: DOSBox's own key, on the clock and after a save.
                due = [t for t in after_save if t <= now]
                if key.available and install is not None and (due or (options.every > 0 and now >= next_picture)):
                    if key.press():
                        next_picture = now + options.every
                        after_save = [t for t in after_save if t > now]
                # The video, once for each game: eight seconds after its window comes to the front, does OBS see it?
                # A player in a full-screen game can't read this window, so a failure is also three beeps.
                if video is not None and key.available:
                    front = key.front_program()
                    game_in_front = "gaius" if front.startswith("gaius") else ""
                    if front.startswith("dosbox") and key.program_in_title(key.front_title()):
                        game_in_front = "original"  # the game itself, not DOSBox starting
                    for name in list(in_front_since):
                        if name != game_in_front:
                            del in_front_since[name]
                    if game_in_front and game_in_front not in checked:
                        if now - in_front_since.setdefault(game_in_front, now) >= 8.0:
                            checked.add(game_in_front)
                            seen = video.sees()
                            if seen:
                                print("video: OBS has the picture of %s" % game_in_front)
                            else:
                                print("video: OBS is NOT getting a picture of %s (a black frame); the saves and the "
                                      "screenshots go on" % game_in_front)
                                import winsound
                                for _ in range(3):
                                    winsound.MessageBeep(winsound.MB_ICONHAND)
                                    time.sleep(0.4)
                for mover in movers.values():
                    mover.poll()
                time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        time.sleep(0.6)
        for mover in movers.values():
            mover.poll()
        if video is not None:
            video.stop()
        if game is not None and game.poll() is None:
            print("gaius: still running; its pictures after this are left in %s" % gaius_captures)
    minutes = (datetime.now() - started).total_seconds() / 60.0
    print("\n%.0f minutes in %s" % (minutes, session))
    for name in watchers:
        print("  %-8s %d saves, %d pictures" % (name, counts[name], movers[name].moved))
    return 0


if __name__ == "__main__":
    main_wrapper(main)
