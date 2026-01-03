import os
import sys
import json
import time
import psutil
import subprocess
import threading
from pystray import Icon, MenuItem, Menu
from PIL import Image, ImageDraw
import tkinter as tk
from tkinter import simpledialog, messagebox

APP_NAME = "AutoRecordOBS"
CONFIG_FILE = "config.json"
OBS_CMD_NAME = "obs-cmd.exe"

recording = False
monitoring = True
automation_enabled = True
active_game = "None"
config = {}
config_lock = threading.Lock()

# =========================
# PATH HELPERS
# =========================
def base_path():
    if getattr(sys, "frozen", False):
        return sys._MEIPASS
    return os.path.dirname(os.path.abspath(__file__))

def obs_cmd_path():
    return os.path.join(base_path(), OBS_CMD_NAME)

# =========================
# ICONS
# =========================
def icon_circle(color):
    img = Image.new("RGB", (64, 64), "white")
    d = ImageDraw.Draw(img)
    d.ellipse((8, 8, 56, 56), fill=color)
    return img

# =========================
# CONFIG
# =========================
def default_config():
    return {
        "check_interval": 2,
        "start_delay": 0,
        "games": {
            "VRChat.exe": {} # Example
        }
    }

def load_config():
    path = os.path.join(base_path(), CONFIG_FILE)
    if not os.path.exists(path):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(default_config(), f, indent=4)
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)

def save_config():
    with open(os.path.join(base_path(), CONFIG_FILE), "w", encoding="utf-8") as f:
        json.dump(config, f, indent=4)

def reload_config(icon=None):
    global config
    with config_lock:
        config = load_config()
    if icon:
        icon.title = "Config reloaded"

# =========================
# GAME DETECTION
# =========================
def running_games(game_list):
    found = []
    for p in psutil.process_iter(["name"]):
        if p.info["name"] in game_list:
            found.append(p.info["name"])
    return found

# =========================
# OBS CONTROL
# =========================
def obs(args):
    subprocess.Popen(
        [obs_cmd_path()] + args,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

# =========================
# MONITOR THREAD
# =========================
def monitor(icon):
    global recording, active_game
    delay_start = None

    while monitoring:
        if not automation_enabled:
            time.sleep(1)
            continue

        with config_lock:
            games = list(config["games"].keys())
            delay = config.get("start_delay", 0)
            interval = config.get("check_interval", 2)

        running = running_games(games)

        if running and not recording:
            if delay_start is None:
                delay_start = time.time()
            elif time.time() - delay_start >= delay:
                active_game = running[0]

                obs(["recording", "start"])

                recording = True
                icon.icon = icon_circle("green")
                icon.title = f"Recording {active_game}"

        elif not running and recording:
            obs(["recording", "stop"])
            recording = False
            delay_start = None
            active_game = "None"
            icon.icon = icon_circle("red")
            icon.title = "Idle"

        time.sleep(interval)

# =========================
# TRAY ACTIONS
# =========================
def edit_config(icon, item):
    os.startfile(os.path.join(base_path(), CONFIG_FILE))

def toggle_automation(icon, item):
    global automation_enabled
    automation_enabled = not automation_enabled
    icon.title = "Paused" if not automation_enabled else "Active"
    icon.icon = icon_circle("orange" if not automation_enabled else "red" if not recording else "green")

def add_game(icon, item):
    root = tk.Tk()
    root.withdraw()

    exe = simpledialog.askstring("Add Game", "Game executable (example: game.exe)")
    if not exe:
        return

    with config_lock:
        config["games"][exe] = {}
        save_config()

    messagebox.showinfo("Added", f"{exe} added successfully")

def exit_app(icon, item):
    global monitoring
    monitoring = False
    if recording:
        obs(["recording", "stop"])
    icon.stop()

# =========================
# TRAY MENU
# =========================
def tray_menu():
    return Menu(
        MenuItem(lambda _: f"Recording: {active_game}" if recording else "Idle", None, enabled=False),
        Menu.SEPARATOR,
        # MenuItem("➕ Add Game", add_game),
        MenuItem("⚙ Edit Config", edit_config),
        MenuItem("🔄 Reload Config", lambda i, _: reload_config(i)),
        MenuItem("⏸ Pause Automation", toggle_automation, checked=lambda _: not automation_enabled),
        Menu.SEPARATOR,
        MenuItem("❌ Exit", exit_app)
    )

# =========================
# MAIN
# =========================
config = load_config()

icon = Icon(
    APP_NAME,
    icon_circle("red"),
    "Idle",
    tray_menu()
)

threading.Thread(target=monitor, args=(icon,), daemon=True).start()
icon.run()