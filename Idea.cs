using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Windows.Forms;
using System.Collections.Generic;
using System.Drawing;
using Microsoft.Win32;

// --- Configuration Models ---
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
    public Dictionary<string, GameConfig> games { get; set; } = new Dictionary<string, GameConfig>();
}

// --- The Add Game Dialog (Fixed & Restored) ---
class AddGameForm : Form
{
    public string SelectedExe { get; private set; }
    ComboBox exeDropdown = new ComboBox();
    TextBox manualInput = new TextBox();
    Button okBtn = new Button();
    Button cancelBtn = new Button();

    public AddGameForm()
    {
        Text = "Add Game - Scanner Active";
        Width = 500; Height = 280;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterParent;

        var lbl1 = new Label { Text = "Select scanned game:", Top = 10, Left = 10, Width = 450 };
        exeDropdown.SetBounds(10, 30, 460, 25);
        exeDropdown.DropDownStyle = ComboBoxStyle.DropDownList;

        var lbl2 = new Label { Text = "Or type process name (e.g. EldenRing.exe):", Top = 70, Left = 10, Width = 450 };
        manualInput.SetBounds(10, 90, 460, 25);

        okBtn.Text = "Add Game"; okBtn.SetBounds(130, 180, 100, 35);
        cancelBtn.Text = "Cancel"; cancelBtn.SetBounds(250, 180, 100, 35);

        Controls.AddRange(new Control[] { lbl1, exeDropdown, lbl2, manualInput, okBtn, cancelBtn });

        okBtn.Click += (s, e) => {
            SelectedExe = !string.IsNullOrWhiteSpace(manualInput.Text) ? manualInput.Text : exeDropdown.SelectedItem?.ToString();
            if (!string.IsNullOrEmpty(SelectedExe)) { DialogResult = DialogResult.OK; Close(); }
        };
        
        LoadInstalledGames();
    }

    void LoadInstalledGames() {
        var exes = new List<string>();
        // Steam
        string steam = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam", "steamapps", "common");
        if (Directory.Exists(steam)) 
            foreach (var dir in Directory.GetDirectories(steam)) exes.AddRange(SafeEnumerate(dir, "*.exe", 2));
        
        // Epic
        string epic = @"C:\Program Files\Epic Games";
        if (Directory.Exists(epic)) 
            foreach (var dir in Directory.GetDirectories(epic)) exes.AddRange(SafeEnumerate(dir, "*.exe", 2));

        var filtered = exes.Select(Path.GetFileName).Distinct()
            .Where(f => !f.ToLower().Contains("unins") && !f.ToLower().Contains("setup"))
            .OrderBy(x => x).ToArray();

        exeDropdown.Items.AddRange(filtered);
    }

    IEnumerable<string> SafeEnumerate(string dir, string pattern, int maxDepth, int currentDepth = 0) {
        if (currentDepth > maxDepth) yield break;
        string[] files = { };
        try { files = Directory.GetFiles(dir, pattern); } catch { }
        foreach (var f in files) yield return f;
        string[] dirs = { };
        try { dirs = Directory.GetDirectories(dir); } catch { }
        foreach (var d in dirs) foreach (var f in SafeEnumerate(d, pattern, maxDepth, currentDepth + 1)) yield return f;
    }
}

// --- Main Application ---
class MainForm : Form
{
    private Config config;
    private bool recording = false, automationEnabled = true, allowExit = false;
    private string activeGame = "None";
    
    // UI Elements
    private DataGridView gameGrid = new DataGridView();
    private ListBox logBox = new ListBox();
    private NotifyIcon trayIcon;
    private Label statusLabel = new Label();
    private Button toggleBtn = new Button();

    public MainForm()
    {
        config = LoadConfig();
        InitializeInterface();
        SetupTray();
        
        new Thread(MonitorLoop) { IsBackground = true }.Start();
    }

    private void InitializeInterface()
    {
        Text = "AutoRecordOBS Innovation Engine";
        Size = new Size(750, 550);
        BackColor = Color.FromArgb(25, 25, 25);
        ForeColor = Color.White;

        statusLabel.SetBounds(20, 10, 500, 30);
        statusLabel.Font = new Font("Segoe UI", 12f, FontStyle.Bold);
        statusLabel.Text = "SYSTEM READY";

        gameGrid.SetBounds(20, 50, 690, 250);
        gameGrid.BackgroundColor = Color.FromArgb(40, 40, 40);
        gameGrid.ForeColor = Color.Black;
        gameGrid.Columns.Add("exe", "Game Executable");
        gameGrid.Columns.Add("start", "Start Delay");
        gameGrid.Columns.Add("stop", "Stop Delay");
        foreach(var g in config.games) gameGrid.Rows.Add(g.Key, g.Value.start_delay, g.Value.stop_delay);

        var addBtn = new Button { Text = "➕ Add Game", Left = 20, Top = 310, Width = 120, FlatStyle = FlatStyle.Flat };
        addBtn.Click += (s, e) => {
            using (var f = new AddGameForm())
                if (f.ShowDialog() == DialogResult.OK) gameGrid.Rows.Add(f.SelectedExe, "default", "default");
        };

        var saveBtn = new Button { Text = "💾 Save Config", Left = 150, Top = 310, Width = 120, FlatStyle = FlatStyle.Flat };
        saveBtn.Click += (s, e) => SaveData();

        toggleBtn.SetBounds(280, 310, 150, 23);
        toggleBtn.Text = "Pause Automation";
        toggleBtn.Click += (s, e) => { automationEnabled = !automationEnabled; Log(automationEnabled ? "Resumed" : "Paused"); };

        logBox.SetBounds(20, 350, 690, 120);
        logBox.BackColor = Color.Black;
        logBox.ForeColor = Color.Cyan;

        Controls.AddRange(new Control[] { statusLabel, gameGrid, addBtn, saveBtn, toggleBtn, logBox });
    }

    public void Log(string msg) {
        if (InvokeRequired) { Invoke(new Action(() => Log(msg))); return; }
        logBox.Items.Insert(0, $"[{DateTime.Now:HH:mm:ss}] {msg}");
    }

    private void SaveData() {
        config.games.Clear();
        foreach (DataGridViewRow r in gameGrid.Rows) {
            if (r.Cells[0].Value == null) continue;
            config.games[r.Cells[0].Value.ToString()] = new GameConfig { start_delay = r.Cells[1].Value, stop_delay = r.Cells[2].Value };
        }
        File.WriteAllText("config.json", JsonSerializer.Serialize(config));
        Log("Configuration Saved.");
    }

    private void MonitorLoop()
    {
        while (true)
        {
            Thread.Sleep(config.check_interval * 1000);
            if (!automationEnabled) continue;

            var running = Process.GetProcesses()
                .Where(p => config.games.ContainsKey(p.ProcessName + ".exe"))
                .Select(p => p.ProcessName + ".exe").ToArray();

            if (running.Length > 0 && !recording) {
                activeGame = running[0];
                Log($"Match Found: {activeGame}. Starting...");
                Obs("recording start");
                recording = true;
            } 
            else if (running.Length == 0 && recording) {
                Log("Game Closed. Stopping...");
                Obs("recording stop");
                recording = false;
                activeGame = "None";
            }

            Invoke(new Action(() => {
                statusLabel.Text = recording ? $"🔴 RECORDING: {activeGame}" : "⚪ STATUS: IDLE";
                statusLabel.ForeColor = recording ? Color.Red : Color.Lime;
            }));
        }
    }

    private void Obs(string args) {
        try {
            Process.Start(new ProcessStartInfo { FileName = "obs-cmd.exe", Arguments = args, CreateNoWindow = true, UseShellExecute = false });
        } catch { Log("Error: obs-cmd.exe missing!"); }
    }

    private void SetupTray() {
        trayIcon = new NotifyIcon { Icon = SystemIcons.Application, Visible = true, Text = "AutoRecordOBS" };
        var menu = new ContextMenuStrip();
        menu.Items.Add("Show", null, (s, e) => Show());
        menu.Items.Add("Exit", null, (s, e) => { allowExit = true; Application.Exit(); });
        trayIcon.ContextMenuStrip = menu;
    }

    private Config LoadConfig() {
        if (!File.Exists("config.json")) return new Config();
        return JsonSerializer.Deserialize<Config>(File.ReadAllText("config.json"));
    }

    protected override void OnFormClosing(FormClosingEventArgs e) {
        if (!allowExit) { e.Cancel = true; Hide(); }
        base.OnFormClosing(e);
    }
}

// Entry Point
static class Program {
    [STAThread] static void Main() {
        Application.EnableVisualStyles();
        Application.Run(new MainForm());
    }
}