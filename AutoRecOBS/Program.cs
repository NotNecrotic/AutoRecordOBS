using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Windows.Forms;
using System.Collections.Generic;
using System.Drawing;

class GameConfig
{
    public object start_delay { get; set; } = "default";
    public object stop_delay { get; set; } = "default";
}

class Config
{
    public int check_interval { get; set; } = 2;
    public int start_delay { get; set; } = 2;
    public int stop_delay { get; set; } = 2;

    public bool start_with_windows { get; set; } = false;
    public bool use_replay_buffer { get; set; } = false;
    public bool pause_when_minimized { get; set; } = true;
    public bool first_run { get; set; } = true;

    public Dictionary<string, GameConfig> games { get; set; } = new Dictionary<string, GameConfig>()
    {
        { "Game.exe", new GameConfig() }
    };
}

class AddGameForm : Form
{
    public string SelectedExe { get; private set; } = null;

    ComboBox exeDropdown = new ComboBox();
    TextBox manualInput = new TextBox();
    Button okBtn = new Button();
    Button cancelBtn = new Button();
    Label label1 = new Label();
    Label label2 = new Label();

    public AddGameForm()
    {
        Text = "Add Game";
        Width = 500;
        Height = 250;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;

        // Dropdown label
        label1.Text = "Select from installed games:";
        label1.Top = 10; label1.Left = 10; label1.Width = 450;
        Controls.Add(label1);

        // Dropdown
        exeDropdown.Top = 30; exeDropdown.Left = 10; exeDropdown.Width = 450;
        Controls.Add(exeDropdown);

        // Manual label
        label2.Text = "Or type the .exe manually:";
        label2.Top = 70; label2.Left = 10; label2.Width = 450;
        Controls.Add(label2);

        // Manual input
        manualInput.Top = 90; manualInput.Left = 10; manualInput.Width = 450;
        Controls.Add(manualInput);

        // Buttons
        okBtn.Text = "OK"; okBtn.Top = 140; okBtn.Left = 100; okBtn.Width = 100;
        cancelBtn.Text = "Cancel"; cancelBtn.Top = 140; cancelBtn.Left = 250; cancelBtn.Width = 100;
        Controls.Add(okBtn);
        Controls.Add(cancelBtn);

        okBtn.Click += (s, e) =>
        {
            if (!string.IsNullOrEmpty(manualInput.Text))
                SelectedExe = manualInput.Text;
            else if (exeDropdown.SelectedItem != null)
                SelectedExe = exeDropdown.SelectedItem.ToString();

            if (!string.IsNullOrEmpty(SelectedExe))
            {
                DialogResult = DialogResult.OK;
                Close();
            }
        };

        cancelBtn.Click += (s, e) => Close();

        LoadInstalledGames();
    }

    void LoadInstalledGames()
    {
        exeDropdown.Items.AddRange(GetInstalledGamesExe().ToArray());
    }

    List<string> GetInstalledGamesExe()
    {
        var exes = new List<string>();

        // Steam games
        string steamCommon = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam", "steamapps", "common");
        if (Directory.Exists(steamCommon))
        {
            foreach (var gameFolder in Directory.GetDirectories(steamCommon))
            {
                // Only scan THIS folder + 2 levels max
                exes.AddRange(SafeEnumerate(gameFolder, "*.exe", 2));
            }
        }

        // Epic Games
        string epicPath = @"C:\Program Files\Epic Games";
        if (Directory.Exists(epicPath))
        {
            foreach (var gameFolder in Directory.GetDirectories(epicPath))
            {
                exes.AddRange(SafeEnumerate(gameFolder, "*.exe", 2));
            }
        }

        // Filter helpers / tiny exes
        var ignoreNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        { "unins000.exe", "setup.exe", "launcher.exe", "redist.exe" };

        var filtered = exes.Where(exe =>
        {
            string name = Path.GetFileName(exe);
            return !ignoreNames.Contains(name.ToLower()) && new FileInfo(exe).Length > 20_000_000;
        });

        return filtered.Select(Path.GetFileName)
                    .Distinct()
                    .OrderBy(x => x)
                    .ToList();
    }

    IEnumerable<string> SafeEnumerate(string dir, string pattern, int maxDepth, int currentDepth = 0)
    {
        if (currentDepth > maxDepth) yield break;

        string[] files = Array.Empty<string>();
        string[] subdirs = Array.Empty<string>();

        try { files = Directory.GetFiles(dir, pattern); } catch { }
        foreach (var f in files) yield return f;

        try { subdirs = Directory.GetDirectories(dir); } catch { }
        foreach (var d in subdirs)
        {
            foreach (var f in SafeEnumerate(d, pattern, maxDepth, currentDepth + 1))
                yield return f;
        }
    }
}

static class Program
{
    static string CONFIG_FILE = "config.json";
    static string OBS_CMD = "obs-cmd.exe";

    static Config config;
    static bool recording = false;
    static bool automationEnabled = true;
    static bool monitoring = true;
    static string activeGame = "None";

    [STAThread]
    static void Main()
    {
        config = LoadConfig();

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        Application.Run(new MainForm());
    }

    static Config LoadConfig()
    {
        if (!File.Exists(CONFIG_FILE))
        {
            var def = new Config();
            File.WriteAllText(CONFIG_FILE, JsonSerializer.Serialize(def, new JsonSerializerOptions { WriteIndented = true }));
            return def;
        }

        return JsonSerializer.Deserialize<Config>(File.ReadAllText(CONFIG_FILE));
    }

    static bool ObsRunning()
    {
        return Process.GetProcesses()
            .Any(p => p.ProcessName.ToLower().Contains("obs"));
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern int GetWindowThreadProcessId(IntPtr hWnd, out int processId);

    static bool IsGameFocused(string exe)
    {
        var handle = GetForegroundWindow();
        GetWindowThreadProcessId(handle, out int pid);

        try
        {
            var proc = Process.GetProcessById(pid);
            return (proc.ProcessName + ".exe")
                .Equals(exe, StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            return false;
        }
    }

    static void Obs(string args)
    {
        Process.Start(new ProcessStartInfo
        {
            FileName = OBS_CMD,
            Arguments = args,
            CreateNoWindow = true,
            UseShellExecute = false
        });
    }

    static void StartRecording()
    {
        if (config.use_replay_buffer)
            Obs("replaybuffer start");
        else
            Obs("recording start");

        recording = true;
    }

    static void StopRecording()
    {
        if (config.use_replay_buffer)
            Obs("replaybuffer stop");
        else
            Obs("recording stop");

        recording = false;
    }

    static string[] RunningGames()
    {
        return Process.GetProcesses()
            .Where(p => config.games.ContainsKey(p.ProcessName + ".exe"))
            .Select(p => p.ProcessName + ".exe")
            .ToArray();
    }

    // ==============================
    // Main GUI Form
    // ==============================
   class MainForm : Form
    {
        DataGridView gameGrid = new DataGridView();
        Button addBtn = new Button();
        Button removeBtn = new Button();
        Button saveBtn = new Button();
        Button toggleAutomationBtn = new Button();
        Label statusLabel = new Label();
        NumericUpDown globalStartDelay = new NumericUpDown();
        NumericUpDown globalStopDelay = new NumericUpDown();

        NotifyIcon trayIcon;
        ContextMenuStrip trayMenu;
        bool allowExit = false;

        Icon idleIcon;
        Icon recordingIcon;

        ListBox logBox = new ListBox();

        public MainForm()
        {
            Text = "AutoRecordOBS GUI";
            Width = 700;
            Height = 500;

            // Status
            statusLabel.Top = 10;
            statusLabel.Left = 10;
            statusLabel.Width = 400;
            statusLabel.Text = "Status: Idle";
            Controls.Add(statusLabel);

            // Game grid
            gameGrid.Top = 40;
            gameGrid.Left = 10;
            gameGrid.Width = 660;
            gameGrid.Height = 300;
            gameGrid.Columns.Add("exe", "Game EXE");
            gameGrid.Columns.Add("start", "Start Delay");
            gameGrid.Columns.Add("stop", "Stop Delay");
            LoadGames();
            Controls.Add(gameGrid);

            // Buttons
            addBtn.Text = "Add Game"; addBtn.Top = 350; addBtn.Left = 10;
            removeBtn.Text = "Remove Game"; removeBtn.Top = 350; removeBtn.Left = 100;
            saveBtn.Text = "Save Config"; saveBtn.Top = 350; saveBtn.Left = 210;
            toggleAutomationBtn.Text = "Pause Automation"; toggleAutomationBtn.Top = 350; toggleAutomationBtn.Left = 340;

            addBtn.Click += (s, e) => AddGame();
            removeBtn.Click += (s, e) => RemoveGame();
            saveBtn.Click += (s, e) => SaveConfig();
            toggleAutomationBtn.Click += (s, e) => ToggleAutomation();

            Controls.Add(addBtn);
            Controls.Add(removeBtn);
            Controls.Add(saveBtn);
            Controls.Add(toggleAutomationBtn);

            // Global delay controls
            Label lblStart = new Label() { Top = 390, Left = 10, Text = "Global Start Delay:" };
            Label lblStop = new Label() { Top = 420, Left = 10, Text = "Global Stop Delay:" };
            globalStartDelay.Top = 390; globalStartDelay.Left = 150; globalStartDelay.Value = config.start_delay;
            globalStopDelay.Top = 420; globalStopDelay.Left = 150; globalStopDelay.Value = config.stop_delay;

            Controls.Add(lblStart);
            Controls.Add(lblStop);
            Controls.Add(globalStartDelay);
            Controls.Add(globalStopDelay);

            // Live activity
            logBox.Top = 430;
            logBox.Left = 10;
            logBox.Width = 660;
            logBox.Height = 60;
            Controls.Add(logBox);

            // =========================
            // Tray setup
            // =========================
            idleIcon = CreateCircleIcon(Color.Gray);
            recordingIcon = CreateCircleIcon(Color.Red);

            trayMenu = new ContextMenuStrip();
            trayMenu.Items.Add("Show", null, (s, e) =>
            {
                this.Show();
                this.WindowState = FormWindowState.Normal;
            });
            trayMenu.Items.Add("Exit", null, (s, e) =>
            {
                allowExit = true;
                trayIcon.Visible = false;
                Application.Exit();
            });

            trayMenu.Items.Add("Start Recording", null, (s, e) =>
            {
                StartRecording();
                Log("▶ Manual start");
            });

            trayMenu.Items.Add("Stop Recording", null, (s, e) =>
            {
                StopRecording();
                Log("⏹ Manual stop");
            });

            trayIcon = new NotifyIcon()
            {
                Text = "AutoRecordOBS - Idle",
                Icon = idleIcon,
                ContextMenuStrip = trayMenu,
                Visible = true
            };

            trayIcon.DoubleClick += (s, e) =>
            {
                this.Show();
                this.WindowState = FormWindowState.Normal;
            };

            if (config.first_run)
            {
                MessageBox.Show(
                    "Welcome!\n\n1. Make sure OBS is running\n2. Add your games\n3. Enable replay buffer if desired",
                    "First Time Setup"
                );

                config.first_run = false;
                File.WriteAllText("config.json", JsonSerializer.Serialize(config, new JsonSerializerOptions { WriteIndented = true }));
            }

            // Start monitor loop
            new Thread(MonitorLoop) { IsBackground = true }.Start();
        }

        void Log(string msg)
            {
                Invoke(new Action(() =>
                {
                    string line = $"[{DateTime.Now:HH:mm:ss}] {msg}";
                    logBox.Items.Insert(0, line);

                    if (logBox.Items.Count > 100)
                        logBox.Items.RemoveAt(logBox.Items.Count - 1);
                }));
            }

        // =========================
        // Generate Icons
        // =========================      
        Icon CreateCircleIcon(Color color)
        {
            int size = 16;
            Bitmap bmp = new Bitmap(size, size);

            using (Graphics g = Graphics.FromImage(bmp))
            {
                g.Clear(Color.Transparent);
                using (Brush brush = new SolidBrush(color))
                {
                    g.FillEllipse(brush, 2, 2, size - 4, size - 4);
                }
            }

            return Icon.FromHandle(bmp.GetHicon());
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (!allowExit && e.CloseReason == CloseReason.UserClosing)
            {
                e.Cancel = true;
                this.Hide();

                trayIcon.ShowBalloonTip(
                    1000,
                    "Still running",
                    "App minimized to tray",
                    ToolTipIcon.Info
                );
            }
            else
            {
                trayIcon.Visible = false;
            }

            base.OnFormClosing(e);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);

            if (this.WindowState == FormWindowState.Minimized)
            {
                this.Hide();
            }
        }

        void LoadGames()
        {
            gameGrid.Rows.Clear();
            foreach (var g in config.games)
            {
                gameGrid.Rows.Add(g.Key, g.Value.start_delay, g.Value.stop_delay);
            }
        }

        void AddGame()
        {
            using (var form = new AddGameForm())
            {
                if (form.ShowDialog() == DialogResult.OK)
                {
                    string exe = form.SelectedExe;
                    if (!string.IsNullOrEmpty(exe) && !gameGrid.Rows
                        .OfType<DataGridViewRow>()
                        .Any(r => r.Cells[0].Value?.ToString() == exe))
                    {
                        gameGrid.Rows.Add(exe, "default", "default");
                    }
                }
            }
        }

        void RemoveGame()
        {
            if (gameGrid.SelectedRows.Count > 0)
                gameGrid.Rows.RemoveAt(gameGrid.SelectedRows[0].Index);
        }

        void SaveConfig()
        {
            config.games.Clear();
            foreach (DataGridViewRow row in gameGrid.Rows)
            {
                if (row.Cells[0].Value == null) continue;
                string exe = row.Cells[0].Value.ToString();
                config.games[exe] = new GameConfig()
                {
                    start_delay = row.Cells[1].Value,
                    stop_delay = row.Cells[2].Value
                };
            }

            config.start_delay = (int)globalStartDelay.Value;
            config.stop_delay = (int)globalStopDelay.Value;

            File.WriteAllText("config.json", JsonSerializer.Serialize(config, new JsonSerializerOptions { WriteIndented = true }));
            MessageBox.Show("Config saved!");
        }

        void ToggleAutomation()
        {
            automationEnabled = !automationEnabled;
            toggleAutomationBtn.Text = automationEnabled ? "Pause Automation" : "Resume Automation";
        }

        void MonitorLoop()
        {
            while (monitoring)
            {
                if (!automationEnabled)
                {
                    Thread.Sleep(1000);
                    continue;
                }

                var running = RunningGames();

                if (running.Length > 0 && !recording)
                {
                    activeGame = running[0];

                    // Focus check
                    if (config.pause_when_minimized && !IsGameFocused(activeGame))
                    {
                        Log($"⏸ {activeGame} not focused");
                    }
                    else
                    {
                        // OBS check
                        if (!ObsRunning())
                        {
                            Log("⚠ OBS not running");
                        }
                        else
                        {
                            // Start recording / replay buffer
                            StartRecording();
                            Log($"▶ Started {(config.use_replay_buffer ? "Replay Buffer" : "Recording")} ({activeGame})");
                        }
                    }
                }
                else if (running.Length == 0 && recording)
                {
                    StopRecording();
                    Log("⏹ Stopped recording");
                    activeGame = "None";
                }

                Invoke(new Action(() =>
                {
                    bool isRecording = recording;

                    statusLabel.Text = $"Status: {(isRecording ? "Recording" : "Idle")} | Active Game: {activeGame}";
                    trayIcon.Text = $"AutoRecordOBS - {(isRecording ? "Recording" : "Idle")}";

                    trayIcon.Icon = isRecording ? recordingIcon : idleIcon;
                }));
                Thread.Sleep(config.check_interval * 1000);
            }
        }
    }
}