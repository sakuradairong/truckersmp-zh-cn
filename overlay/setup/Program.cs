using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

namespace TruckCompanionSetup
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try { Application.Run(new SetupForm()); }
            catch (Exception ex) { MessageBox.Show("安装程序无法打开：\n" + ex.Message, "Truck Companion", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }
        internal static Dictionary<string, object> ReadRecord(string path)
        {
            if (!File.Exists(path) || new FileInfo(path).Length > 32768) throw new IOException("安装记录不存在或过大。");
            return new JavaScriptSerializer { MaxJsonLength = 32768 }.Deserialize<Dictionary<string, object>>(File.ReadAllText(path, Encoding.UTF8));
        }
        internal static string Text(Dictionary<string, object> record, string name)
        {
            object value;
            return record != null && record.TryGetValue(name, out value) ? Convert.ToString(value) : "";
        }
        internal static string ExtractPayload()
        {
            string root = Path.Combine(Path.GetTempPath(), "TruckCompanionSetup-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(root);
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            long total = 0;
            using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
            {
                if (stream == null) throw new IOException("缺少安装文件，请重新下载。");
                using (var zip = new ZipArchive(stream, ZipArchiveMode.Read))
                {
                    if (zip.Entries.Count > 512) throw new IOException("安装包文件数量异常。");
                    foreach (var entry in zip.Entries)
                    {
                        string relative = entry.FullName.Replace('/', '\\');
                        if (Path.IsPathRooted(relative) || relative.IndexOf(':') >= 0) throw new IOException("安装包包含无效路径。");
                        string path = Path.GetFullPath(Path.Combine(root, relative));
                        if (!path.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase) || !names.Add(path)) throw new IOException("安装包包含重复或越界路径。");
                        total += entry.Length;
                        if (total > 64 * 1024 * 1024) throw new IOException("安装包内容过大。");
                        if (relative.EndsWith("\\", StringComparison.Ordinal)) { Directory.CreateDirectory(path); continue; }
                        Directory.CreateDirectory(Path.GetDirectoryName(path));
                        using (var input = entry.Open())
                        using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write)) { input.CopyTo(output); }
                    }
                }
            }
            return root;
        }
        internal static bool IsGame(string path)
        {
            try { return File.Exists(Path.Combine(Path.GetFullPath(path), "bin", "win_x64", "eurotrucks2.exe")); }
            catch { return false; }
        }
        internal static List<string> DiscoverGames()
        {
            var steam = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var games = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string key in new[] { @"HKEY_CURRENT_USER\Software\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam" })
            {
                foreach (string name in new[] { "SteamPath", "InstallPath" })
                {
                    try { var value = Registry.GetValue(key, name, null) as string; if (!String.IsNullOrWhiteSpace(value)) steam.Add(Path.GetFullPath(value)); } catch { }
                }
            }
            steam.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"));
            foreach (string path in new List<string>(steam))
            {
                try
                {
                    string vdf = Path.Combine(path, "steamapps", "libraryfolders.vdf");
                    if (!File.Exists(vdf) || new FileInfo(vdf).Length > 1024 * 1024) continue;
                    foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"((?:\\\\.|[^\"])*)\""))
                        steam.Add(Path.GetFullPath(m.Groups[1].Value.Replace(@"\\", @"\")));
                }
                catch { }
            }
            foreach (string library in steam)
            {
                string path = Path.Combine(library, "steamapps", "common", "Euro Truck Simulator 2");
                if (IsGame(path)) games.Add(path);
            }
            foreach (string key in new[] { @"HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 227300", @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 227300" })
            {
                try { var path = Registry.GetValue(key, "InstallLocation", null) as string; if (!String.IsNullOrEmpty(path) && IsGame(path)) games.Add(Path.GetFullPath(path)); } catch { }
            }
            var result = new List<string>(games); result.Sort(StringComparer.OrdinalIgnoreCase); return result;
        }
        internal static string Quote(string value)
        {
            var result = new StringBuilder("\""); int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { ++slashes; continue; }
                result.Append('\\', c == '"' ? slashes * 2 + 1 : slashes); result.Append(c); slashes = 0;
            }
            return result.Append('\\', slashes * 2).Append('"').ToString();
        }
    }

    sealed class SetupForm : Form
    {
        readonly string payload, version;
        readonly ComboBox game = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDown };
        readonly Label state = new Label { Dock = DockStyle.Fill, AutoSize = false };
        readonly TextBox log = new TextBox { Dock = DockStyle.Fill, Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical };
        readonly Button install = new Button { Text = "安装 / 升级", Width = 150, Height = 38 };
        readonly Button restore = new Button { Text = "回退本次安装", Width = 150, Height = 38 };
        readonly Button disable = new Button { Text = "停用面板", Width = 150, Height = 38 };
        readonly Button browse = new Button { Text = "浏览…", Dock = DockStyle.Fill };
        readonly Button detect = new Button { Text = "重新识别", Dock = DockStyle.Fill };
        readonly ProgressBar progress = new ProgressBar { Dock = DockStyle.Fill, Style = ProgressBarStyle.Blocks };
        bool busy;

        public SetupForm()
        {
            payload = Program.ExtractPayload();
            var info = Program.ReadRecord(Path.Combine(payload, "BUILD-INFO.json"));
            version = Program.Text(info, "version");
            if (Program.Text(info, "product") != "Truck Companion Overlay" || Program.Text(info, "flavour") != "local-freight" || !Version.TryParse(version, out parsedVersion))
                throw new IOException("安装包描述不正确。");
            Text = "Truck Companion " + version + " · 完整实验版安装程序";
            Font = new Font("Microsoft YaHei UI", 9F);
            AutoScaleMode = AutoScaleMode.Dpi;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(760, 610); MinimumSize = new Size(740, 610);
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(22), ColumnCount = 1, RowCount = 9 };
            foreach (int height in new[] { 43, 98, 26, 36, 62, 48, 22 }) layout.RowStyles.Add(new RowStyle(SizeType.Absolute, height));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
            layout.Controls.Add(new Label { Text = "Truck Companion · 完整实验版 " + version, Font = new Font(Font.FontFamily, 17F, FontStyle.Bold), Dock = DockStyle.Fill }, 0, 0);
            layout.Controls.Add(new Label { Text = "附近玩家、速度、个人资料、VTC 和设置；含自选做货与普通 / WoT 任务复制。\n实验状态：货运仍有原版不弹提示的未解决问题，功能未完成游戏内验收。\n本项目为非官方工具；内部货运接入未获 TMP 明确许可。请先退出游戏。", ForeColor = Color.FromArgb(135, 72, 15), Dock = DockStyle.Fill }, 0, 1);
            layout.Controls.Add(new Label { Text = "ETS2 游戏文件夹（例如 SteamLibrary\\steamapps\\common\\Euro Truck Simulator 2）", Dock = DockStyle.Fill }, 0, 2);
            var pathRow = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 3, Margin = Padding.Empty };
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100)); pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 76)); pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 100));
            pathRow.Controls.Add(game, 0, 0); pathRow.Controls.Add(browse, 1, 0); pathRow.Controls.Add(detect, 2, 0); layout.Controls.Add(pathRow, 0, 3);
            layout.Controls.Add(state, 0, 4);
            var actions = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false }; actions.Controls.AddRange(new Control[] { install, restore, disable }); layout.Controls.Add(actions, 0, 5);
            layout.Controls.Add(progress, 0, 6); layout.Controls.Add(log, 0, 7);
            var footer = new FlowLayoutPanel { Dock = DockStyle.Fill, WrapContents = false };
            var help = new Button { Text = "安装 / 使用说明", Width = 155, Height = 30 };
            var logs = new Button { Text = "打开安装日志目录", Width = 155, Height = 30 };
            footer.Controls.Add(help); footer.Controls.Add(logs); layout.Controls.Add(footer, 0, 8); Controls.Add(layout);
            help.Click += delegate { OpenFile(Path.Combine(payload, "使用说明.txt")); };
            logs.Click += delegate { OpenFile(payload); };
            browse.Click += delegate { using (var picker = new FolderBrowserDialog { Description = "选择 Euro Truck Simulator 2 游戏文件夹", ShowNewFolderButton = false }) { if (Directory.Exists(game.Text)) picker.SelectedPath = game.Text; if (picker.ShowDialog(this) == DialogResult.OK) game.Text = picker.SelectedPath; } };
            detect.Click += delegate { Discover(); }; game.TextChanged += delegate { RefreshState(); };
            install.Click += async delegate { await Operate("Install"); };
            restore.Click += async delegate { await Operate("Restore"); };
            disable.Click += async delegate { await Operate("Disable"); };
            FormClosing += delegate(object sender, FormClosingEventArgs e) { if (busy) { e.Cancel = true; MessageBox.Show(this, "正在处理文件，请等待完成后关闭。", "安装中"); } };
            log.Text = "安装会自动备份旧版并保留配置。已有本项目旧数据桥会备份停用；其他作者的插件和原速度工具箱保留。\r\n回退会恢复当前安装记录保存的旧文件；首次安装的回退会移除本面板。\r\n“停用面板”只移走本 DLL，保留配置与备份，不恢复旧插件。\r\n本程序不启动游戏，不执行游戏功能或测试。";
            Discover();
        }
        Version parsedVersion;
        void OpenFile(string path)
        {
            try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "无法打开"); }
        }
        void Discover()
        {
            string prior = game.Text; var found = Program.DiscoverGames(); game.Items.Clear();
            foreach (string path in found) game.Items.Add(path);
            if (Program.IsGame(prior)) game.Text = prior;
            else if (found.Count == 1) game.SelectedIndex = 0;
            else game.Text = "";
            RefreshState();
        }
        void RefreshState()
        {
            if (busy) return;
            bool valid = Program.IsGame(game.Text);
            install.Enabled = valid; restore.Enabled = false; disable.Enabled = false;
            if (!valid) { state.Text = game.Items.Count > 1 ? "找到多个游戏目录，请选择本次使用的 ETS2。" : "请浏览选择游戏目录。可从 Steam → 游戏属性 → 已安装文件 → 浏览找到。"; return; }
            string root = Path.GetFullPath(game.Text);
            bool dll = File.Exists(Path.Combine(root, "bin", "win_x64", "plugins", "TruckCompanionOverlay.dll"));
            bool record = File.Exists(Path.Combine(root, "companion-overlay-active.json"));
            restore.Enabled = record; disable.Enabled = dll;
            state.Text = (dll ? "已发现本面板，安装将自动备份替换。" : record ? "插件目录中未找到本面板；原安装记录仍保留，可重新安装或回退。" : "游戏目录有效，可以安装。") + "\n仅支持 Windows x64、ETS2 + TruckersMP（DX11）。升级保留个人设置。";
        }
        async Task Operate(string action)
        {
            if (busy || !Program.IsGame(game.Text)) return;
            string root = Path.GetFullPath(game.Text); busy = true;
            install.Enabled = restore.Enabled = disable.Enabled = browse.Enabled = detect.Enabled = game.Enabled = false;
            progress.Style = ProgressBarStyle.Marquee; state.Text = "正在处理，请勿启动游戏或关闭安装程序…";
            try
            {
                var result = await Task.Run(() => RunWorker(action, root));
                log.Text = result.Item1 + "\r\n\r\n" + result.Item2;
                MessageBox.Show(this, result.Item1, result.Item3 ? "操作完成" : "操作未完成", MessageBoxButtons.OK, result.Item3 ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
            }
            catch (Exception ex) { log.Text = "操作未完成：" + ex.Message; MessageBox.Show(this, log.Text, "操作未完成", MessageBoxButtons.OK, MessageBoxIcon.Warning); }
            finally { busy = false; browse.Enabled = detect.Enabled = game.Enabled = true; progress.Style = ProgressBarStyle.Blocks; RefreshState(); }
        }
        Tuple<string, string, bool> RunWorker(string action, string root)
        {
            string resultPath = Path.Combine(payload, "operation.json");
            if (File.Exists(resultPath)) File.Delete(resultPath); // Our own per-launch temporary report only.
            var output = new StringBuilder(); object sync = new object();
            var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe"))
            {
                Arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + Program.Quote(Path.Combine(payload, "worker.ps1")) + " -Action " + action + " -GameRoot " + Program.Quote(root),
                WorkingDirectory = payload, UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
                RedirectStandardOutput = true, RedirectStandardError = true, StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
            };
            int exitCode;
            using (var process = new Process { StartInfo = start })
            {
                DataReceivedEventHandler append = delegate(object sender, DataReceivedEventArgs e) { if (e.Data != null) lock (sync) { if (output.Length < 65536) output.AppendLine(e.Data); } };
                process.OutputDataReceived += append; process.ErrorDataReceived += append;
                process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine(); process.WaitForExit(); exitCode = process.ExitCode;
            }
            string details = output.ToString(); File.WriteAllText(Path.Combine(payload, "worker.log"), details, Encoding.UTF8);
            if (!File.Exists(resultPath)) return Tuple.Create("安装脚本未返回结果。请保留日志目录并联系发布者。", details, false);
            var result = Program.ReadRecord(resultPath); object value;
            bool success = exitCode == 0 && result.TryGetValue("success", out value) && value is bool && (bool)value;
            string receipt = Program.Text(result, "receipt");
            if (!String.IsNullOrEmpty(receipt)) details += "\r\n安装 / 回退记录：" + receipt;
            return Tuple.Create(Program.Text(result, "message"), details, success);
        }
    }
}
