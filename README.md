# AutoRecordOBS

AutoRecordOBS is a system-tray-based utility that automatically detects when your games are running and tells OBS to start and stop recording.

## Features

- Automatic game detection: Detects games running and starts recording automatically.
- System tray control: Minimal UI in the tray for quick access, status updates, and control.
- Per-game recording: Supports configuring multiple games.
- Start delay: Delay before recording begins to avoid unnecessary captures such as loading screens.
- Toggle automation: Pause/resume automatic recording at any time.
- Config reload & edit: Edit JSON config directly from the tray, and reload without restarting the app.
- Start at boot: Is able to start when windows has booted.

## Installation

Download the latest release .exe from GitHub Releases.

Ensure OBS is running with the WebSocket plugin active.
> In the latest version of OBS, this can be done by navigating to **Tools > WebSocket Server Settings** and enabling the **WebSocket Server**.

Run AutoRecordOBS.exe. The app will appear in your system tray.

> ⚠️ For now, authentication must be disabled for the **websocket server**.

## Configuration

All settings are stored in config.json (created automatically on first run).

### Example:

```
{
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
```
- ```check_interval``` → How often (seconds) the app checks for running games.
- ```start_delay``` → Delay before starting recording after the game is detected running.
- ```stop_delay``` → Delay before stopping recording after the game is no longer detected running.
- ```games``` → List of game executables to auto-record. (The start and stop delay for each game overrides the main variables if not set to default.)

## Development

1. Clone the repository:
```
git clone https://github.com/NotNecrotic/AutoRecordOBS.git
cd AutoRecordOBS
```

2. Install dependencies:
```
pip install -r requirements.txt
```

3. Run:
```
python AutoRecord.py
```

4. Build standalone exe (requires PyInstaller):
```
python -m PyInstaller --onefile --windowed --add-binary "obs-cmd.exe;." AutoRecord.py
```

## Notes

- OBS must be running and accessible via obs-cmd.
- Currently supports Windows only.
- No console window will appear when running the .exe (uses --windowed).
