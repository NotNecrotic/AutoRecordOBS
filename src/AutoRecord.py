import os
import sys
import json
import time
import psutil
import subprocess
import threading
from pystray import Icon, MenuItem, Menu
from PIL import Image, ImageDraw
import winshell
from win32com.client import Dispatch

# ============================================================
# APP CONSTANTS
# ============================================================

APP_NAME = "AutoRecordOBS"
CONFIG_FILE = "config.json"
OBS_CMD_NAME = "obs-cmd.exe"

# ============================================================
# RUNTIME STATE VARIABLES
# (These describe what the app is doing RIGHT NOW)
# ============================================================

recording = False              # Is OBS currently recording?
monitoring = True              # Controls monitor thread lifetime
automation_enabled = True      # Pause / resume automation
active_game = "None"           # Which game triggered recording

# Used for delay timing (debounce)
start_delay_timer = None
stop_delay_timer = None

# Config is shared between threads
config = {}
config_lock = threading.Lock()

# ============================================================
# PATH HELPERS
# ============================================================

def base_path():
    """Returns app directory (handles PyInstaller builds)."""
    if getattr(sys, "frozen", False):
        return sys._MEIPASS
    return os.path.dirname(os.path.abspath(__file__))

def obs_cmd_path():
    """Full path to obs-cmd executable."""
    return os.path.join(base_path(), OBS_CMD_NAME)

# ============================================================
# TRAY ICON HELPERS
# ============================================================

def icon_circle(color):
    """Creates a colored circular tray icon."""
    img = Image.new("RGB", (64, 64), "white")
    d = ImageDraw.Draw(img)
    d.ellipse((8, 8, 56, 56), fill=color)
    return img

# ============================================================
# CONFIG HANDLING
# ============================================================

def default_config():
    """
    Default configuration file.

    start_delay / stop_delay:
        Global fallback delays (seconds).

    Per-game start_delay / stop_delay:
        Can be a number OR "default" to use global value.
    """
    return {
        "check_interval": 2,
        "start_delay": 2,
        "stop_delay": 2,
        "start_with_windows": False,
        "games": {
            "VRChat.exe": {
                "start_delay": "default",
                "stop_delay": "default"
            }
        }
    }

def load_config():
    """Loads config.json or creates it if missing."""
    path = os.path.join(base_path(), CONFIG_FILE)
    if not os.path.exists(path):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(default_config(), f, indent=4)

    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)

def save_config():
    """Writes current config to disk."""
    with open(os.path.join(base_path(), CONFIG_FILE), "w", encoding="utf-8") as f:
        json.dump(config, f, indent=4)

def reload_config(icon=None):
    """Reloads config at runtime."""
    global config
    with config_lock:
        config = load_config()
        set_startup(config.get("start_with_windows", False))
    if icon:
        icon.title = "Config reloaded"

# ============================================================
# DELAY RESOLUTION LOGIC
# ============================================================

def get_delay(game, key):
    """
    Resolves start_delay or stop_delay for a game.

    If game value is "default", fall back to global value.
    """
    with config_lock:
        value = config["games"].get(game, {}).get(key, "default")
        if value == "default":
            return config.get(key, 0)
        return value

# ============================================================
# WINDOWS STARTUP
# ============================================================

def set_startup(enable):
    """
    Creates or removes a startup shortcut.
    """
    shortcut_path = os.path.join(winshell.startup(), f"{APP_NAME}.lnk")

    if enable:
        shell = Dispatch("WScript.Shell")
        shortcut = shell.CreateShortCut(shortcut_path)
        shortcut.Targetpath = sys.executable
        shortcut.WorkingDirectory = base_path()
        shortcut.save()
    else:
        if os.path.exists(shortcut_path):
            os.remove(shortcut_path)

def toggle_startup(icon, item):
    with config_lock:
        current = config.get("start_with_windows", False)
        new_value = not current
        config["start_with_windows"] = new_value
        save_config()

    set_startup(new_value)

    icon.title = "Startup enabled" if new_value else "Startup disabled"

# ============================================================
# PROCESS / GAME DETECTION
# ============================================================

def running_games(game_list):
    """
    Returns a list of detected running games
    that exist in config["games"].
    """
    found = []
    for p in psutil.process_iter(["name"]):
        if p.info["name"] in game_list:
            found.append(p.info["name"])
    return found

# ============================================================
# OBS CONTROL
# ============================================================

def obs(args):
    """Runs obs-cmd with given arguments."""
    subprocess.Popen(
        [obs_cmd_path()] + args,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

def obs_running():
    """Checks if OBS is currently running."""
    return any(
        p.info["name"] and "obs" in p.info["name"].lower()
        for p in psutil.process_iter(["name"])
    )

def obs_start():
    obs(["recording", "start"])

def obs_stop():
    obs(["recording", "stop"])

# ============================================================
# MAIN MONITOR LOOP
# ============================================================

def monitor(icon):
    """
    Core automation loop.

    Logic:
    - If any configured game is running → start recording (after delay)
    - If no games are running → stop recording (after delay)
    """
    global recording, active_game
    global start_delay_timer, stop_delay_timer

    while monitoring:
        if not automation_enabled:
            time.sleep(1)
            continue

        with config_lock:
            games = list(config["games"].keys())
            interval = config.get("check_interval", 2)

        running = running_games(games)

        # OBS crash recovery
        if recording and not obs_running():
            recording = False
            active_game = "None"
            icon.icon = icon_circle("red")
            icon.title = "OBS stopped"

        # ----------------------------------------------------
        # START RECORDING LOGIC
        # ----------------------------------------------------
        if running and not recording:
            game = running[0]
            delay = get_delay(game, "start_delay")

            if start_delay_timer is None:
                start_delay_timer = time.time()
            elif time.time() - start_delay_timer >= delay and obs_running():
                obs_start()
                recording = True
                active_game = game
                start_delay_timer = None
                stop_delay_timer = None

                icon.icon = icon_circle("green")
                icon.title = f"Recording {active_game}"

        # ----------------------------------------------------
        # STOP RECORDING LOGIC
        # ----------------------------------------------------
        elif not running and recording:
            delay = get_delay(active_game, "stop_delay")

            if stop_delay_timer is None:
                stop_delay_timer = time.time()
            elif time.time() - stop_delay_timer >= delay:
                obs_stop()
                recording = False
                active_game = "None"
                stop_delay_timer = None

                icon.icon = icon_circle("red")
                icon.title = "Idle"

        time.sleep(interval)

# ============================================================
# TRAY MENU ACTIONS
# ============================================================

def edit_config(icon, item):
    os.startfile(os.path.join(base_path(), CONFIG_FILE))

def toggle_automation(icon, item):
    global automation_enabled
    automation_enabled = not automation_enabled
    icon.title = "Paused" if not automation_enabled else "Active"
    icon.icon = icon_circle(
        "orange" if not automation_enabled else "green" if recording else "red"
    )

def exit_app(icon, item):
    global monitoring
    monitoring = False
    if recording:
        obs_stop()
        time.sleep(0.5)
    icon.stop()

# ============================================================
# TRAY MENU
# ============================================================

def tray_menu():
    return Menu(
        MenuItem(lambda _: f"🎮 {active_game}" if recording else "Idle", None, enabled=False),
        MenuItem(lambda _: "⏺ Recording" if recording else "⏹ Not Recording", None, enabled=False),
        Menu.SEPARATOR,
        MenuItem("Start with Windows", toggle_startup, checked=lambda _: config.get("start_with_windows", False)),
        Menu.SEPARATOR,
        MenuItem("⚙ Edit Config", edit_config),
        MenuItem("🔄 Reload Config", lambda i, _: reload_config(i)),
        MenuItem("⏸ Pause Automation", toggle_automation, checked=lambda _: not automation_enabled),
        Menu.SEPARATOR,
        MenuItem("❌ Exit", exit_app)
    )

# ============================================================
# APP ENTRY POINT
# ============================================================

config = load_config()
set_startup(config.get("start_with_windows", False))

icon = Icon(
    APP_NAME,
    icon_circle("red"),
    "Idle",
    tray_menu()
)

threading.Thread(target=monitor, args=(icon,), daemon=True).start()
icon.run()
