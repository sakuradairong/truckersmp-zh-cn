"""Chinese, offline desktop installer for the resource localization only."""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import queue
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from common import ROOT, atomic_json, plain
import installer_backend as backend


class InstallerApp:
    def __init__(self, root, assets=None, state_root=None, scan=True):
        self.root, self.assets, self.state_root = root, assets, state_root
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix='tmpzh-installer')
        self.events = queue.Queue()
        self.busy, self.current_status = False, {}
        self.path = tk.StringVar()
        self.status = tk.StringVar(value='正在查找 TMP 安装目录…' if scan else '请选择 TMP 安装目录')
        self.state_text = tk.StringVar(value='尚未检查')
        root.title('TruckersMP 简体中文安装器')
        root.geometry('850x620')
        root.minsize(790, 600)
        root.configure(bg='#f3f5f7')
        root.option_add('*Font', ('Microsoft YaHei UI', 10))
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('TFrame', background='#f3f5f7')
        style.configure('TLabel', background='#f3f5f7', foreground='#263744', font=('Microsoft YaHei UI', 10))
        style.configure('Title.TLabel', font=('Microsoft YaHei UI', 23, 'bold'), foreground='#123e46')
        style.configure('Sub.TLabel', foreground='#5a6d78')
        style.configure('Card.TFrame', background='white')
        style.configure('Card.TLabel', background='white')
        style.configure('CardTitle.TLabel', background='white', font=('Microsoft YaHei UI', 12, 'bold'))
        style.configure('TButton', font=('Microsoft YaHei UI', 10), padding=(14, 9))
        style.configure('Accent.TButton', background='#176b68', foreground='white', padding=(24, 11))
        style.map('Accent.TButton', background=[('disabled', '#bdc9cc'), ('active', '#205f5e')],
                  foreground=[('disabled', '#eff3f4')])
        frame = ttk.Frame(root, padding=28)
        frame.pack(fill='both', expand=True)
        ttk.Label(frame, text='TruckersMP 简体中文', style='Title.TLabel').pack(anchor='w')
        ttk.Label(frame, text='离线安装 · 自带中文字体 · 随时恢复安装前状态', style='Sub.TLabel').pack(anchor='w', pady=(5, 17))
        card = ttk.Frame(frame, padding=(20, 16), style='Card.TFrame')
        card.pack(fill='x')
        ttk.Label(card, text='本次安装内容', style='CardTitle.TLabel').pack(anchor='w')
        ttk.Label(card, text='Tab 玩家面板、列表与右键菜单\n公交调度窗口、货运窗口的静态标题和按钮',
                  style='Card.TLabel', justify='left').pack(anchor='w', pady=(9, 7))
        ttk.Label(card, text='中文字体已包含；无需安装 Python 或自行寻找字体。', style='Card.TLabel').pack(anchor='w')
        ttk.Label(frame, text='TMP 安装目录', font=('Microsoft YaHei UI', 10, 'bold')).pack(anchor='w', pady=(19, 6))
        path_row = ttk.Frame(frame)
        path_row.pack(fill='x')
        self.path_entry = ttk.Entry(path_row, textvariable=self.path, font=('Microsoft YaHei UI', 10))
        self.path_entry.pack(side='left', fill='x', expand=True, ipady=7)
        self.browse_button = ttk.Button(path_row, text='选择目录', command=self.browse)
        self.browse_button.pack(side='left', padx=(9, 0))
        self.detect_button = ttk.Button(path_row, text='重新检测', command=self.detect)
        self.detect_button.pack(side='left', padx=(9, 0))
        self.path_entry.bind('<Return>', lambda event: self.check_path())
        self.state_label = ttk.Label(frame, textvariable=self.state_text, wraplength=750, style='Sub.TLabel')
        self.state_label.pack(anchor='w', pady=(9, 12))
        ttk.Separator(frame).pack(fill='x')
        actions = ttk.Frame(frame)
        actions.pack(fill='x', pady=(15, 12))
        self.install_button = ttk.Button(actions, text='安装汉化', style='Accent.TButton', command=self.install, state='disabled')
        self.install_button.pack(side='left')
        self.restore_button = ttk.Button(actions, text='恢复安装前状态', command=self.restore, state='disabled')
        self.restore_button.pack(side='left', padx=10)
        self.backup_button = ttk.Button(actions, text='打开备份目录', command=self.open_backups, state='disabled')
        self.backup_button.pack(side='right')
        self.progress = ttk.Progressbar(frame, mode='indeterminate')
        self.progress.pack(fill='x', pady=(0, 8))
        self.status_label = ttk.Label(frame, textvariable=self.status, wraplength=750)
        self.status_label.pack(anchor='w')
        footer = ttk.Frame(frame)
        footer.pack(side='bottom', fill='x', pady=(10, 0))
        ttk.Label(footer, text='请先退出游戏与 TMP 启动器。非官方项目；当前为预览版。',
                  style='Sub.TLabel').pack(anchor='w')
        bottom = ttk.Frame(footer)
        bottom.pack(fill='x', pady=(5, 0))
        ttk.Label(bottom, text='适配 TMP 0.7.9.2 / ETS2 / Windows x64  ·  ' + backend.VERSION,
                  style='Sub.TLabel').pack(side='left')
        ttk.Button(bottom, text='字体与许可', command=self.licenses).pack(side='right')
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(100, self.drain)
        if scan:
            root.after(150, self.detect)

    def set_busy(self, busy):
        self.busy = busy
        for widget in (self.path_entry, self.browse_button, self.detect_button):
            widget.configure(state='disabled' if busy else 'normal')
        self.install_button.configure(state='disabled')
        self.restore_button.configure(state='disabled')
        if busy:
            self.progress.start(12)
        else:
            self.progress.stop()

    def run(self, operation, completed, refresh_on_error=False):
        if self.busy:
            return
        self.set_busy(True)
        future = self.executor.submit(operation)
        future.add_done_callback(lambda value: self.events.put(('done', value, completed, refresh_on_error)))

    def drain(self):
        try:
            while True:
                event = self.events.get_nowait()
                if event[0] == 'progress':
                    self.status.set(event[1])
                    continue
                _, future, completed, refresh_on_error = event
                self.set_busy(False)
                try:
                    result = future.result()
                except Exception as exc:
                    self.status.set(backend.friendly_error(exc))
                    self.state_text.set('操作未完成；请根据提示处理后重新检测。')
                    # Re-detecting after an error keeps recovery available for a journaled interruption.
                    self.current_status = {}
                    if refresh_on_error:
                        self.root.after(100, self.refresh_after_error)
                else:
                    completed(result)
        except queue.Empty:
            pass
        self.root.after(100, self.drain)

    def refresh_after_error(self):
        path = self.path.get().strip()
        if not self.busy and path:
            self.run(lambda: backend.inspect(path, self.state_root), self.apply_status)

    def progress_message(self, text):
        self.events.put(('progress', text))

    def detect(self):
        def operation():
            paths, notes = backend.discover()
            if not paths:
                return None, notes
            return (str(paths[0]), backend.inspect(paths[0], self.state_root)), notes
        self.status.set('正在查找并检查 TMP 安装目录…')
        self.run(operation, self.detected)

    def detected(self, result):
        found, notes = result
        if found is None:
            self.status.set('未找到 TMP，请点击“选择目录”。' + (' ' + notes[0] if notes else ''))
            self.state_text.set('请选择包含 core_ets2mp.dll 的 TMP 安装目录。')
            self.current_status = {}
            return
        path, status = found
        self.path.set(path)
        self.apply_status(status)
        self.status.set('检查完成。' if status['compatible'] else '此客户端暂不受支持，安装按钮已停用。')

    def browse(self):
        path = filedialog.askdirectory(parent=self.root, title='选择 TMP 安装目录（不是 ETS2 游戏目录）', mustexist=True)
        if path:
            self.path.set(path)
            self.check_path()

    def check_path(self):
        path = self.path.get().strip()
        if path:
            self.status.set('正在检查所选目录…')
            self.run(lambda: backend.inspect(path, self.state_root), self.checked)

    def checked(self, status):
        self.apply_status(status)
        self.status.set('检查完成。' if status['compatible'] else '此客户端暂不受支持，安装按钮已停用。')

    def apply_status(self, status):
        self.current_status = status
        labels = {'not_installed': '尚未通过本安装器安装汉化', 'installed': '汉化已安装，可恢复安装前状态',
                  'restored': '已恢复安装前状态，备份仍保留', 'changed': '检测到文件变化，请保留文件并核对后再操作',
                  'applying': '上次安装中断，请先恢复安装前状态', 'restoring': '上次恢复中断，请继续恢复'}
        prefix = '已识别受支持的 TMP 0.7.9.2。' if status['compatible'] else '客户端文件不匹配，暂不支持安装。'
        self.state_text.set(prefix + labels[status['state']])
        self.install_button.configure(state='normal' if status['compatible'] and status['state'] in ('not_installed', 'restored') else 'disabled')
        self.restore_button.configure(state='normal' if status['can_restore'] else 'disabled')
        self.backup_button.configure(state='normal' if status.get('backup_directory') else 'disabled')

    def install(self):
        path = self.path.get().strip()
        self.status.set('正在准备安装…')
        self.run(lambda: backend.install(path, self.assets, self.state_root, self.progress_message),
                 lambda result: self.finished(path, '汉化安装完成。可从 TMP 启动器进入游戏。'), refresh_on_error=True)

    def restore(self):
        path = self.path.get().strip()
        self.status.set('正在准备恢复…')
        self.run(lambda: backend.restore(path, self.state_root, self.progress_message),
                 lambda result: self.finished(path, '已恢复安装前状态。恢复备份继续保留。'), refresh_on_error=True)

    def finished(self, path, message):
        self.status.set(message)
        self.run(lambda: backend.inspect(path, self.state_root), self.apply_status)

    def open_backups(self):
        path = self.current_status.get('backup_directory')
        if path:
            try:
                os.startfile(plain(path))
            except OSError as exc:
                messagebox.showerror('无法打开备份目录', backend.friendly_error(exc), parent=self.root)

    def licenses(self):
        window = tk.Toplevel(self.root)
        window.title('字体与许可')
        window.geometry('760x530')
        window.transient(self.root)
        text = tk.Text(window, wrap='word', padx=16, pady=16, font=('Microsoft YaHei UI', 10))
        scroll = ttk.Scrollbar(window, command=text.yview)
        text.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y'); text.pack(fill='both', expand=True)
        intro = ('汉化代码与翻译：MIT\n字体：SIL Open Font License 1.1\n\n'
                 'TMPZH Sans：Noto Sans SC 的静态 400 / 600 字重。\n'
                 'TMPZH Mono：Noto Sans Mono 的等宽拉丁字符与 Noto Sans SC 中文回退。\n'
                 'OfflineFreightZH：仅供货运静态文案使用的专用映射字体。\n\n')
        folder = Path(self.assets) if self.assets else ROOT / 'assets'
        text.insert('end', intro)
        for name in sorted(backend.LICENSE_FILES):
            try:
                text.insert('end', '\n' + name + '\n' + (folder / name).read_text(encoding='utf-8') + '\n')
            except OSError:
                text.insert('end', '\n许可文件不可读，请重新下载完整安装器。\n')
        notices = ROOT / 'THIRD_PARTY_NOTICES.md'
        if notices.exists():
            text.insert('end', '\n' + notices.read_text(encoding='utf-8'))
        runtime = ROOT / 'runtime-licenses'
        if runtime.is_dir():
            for path in sorted(runtime.rglob('*.txt')):
                text.insert('end', '\n' + path.relative_to(runtime).as_posix() + '\n' + path.read_text(encoding='utf-8', errors='replace'))
        text.configure(state='disabled')

    def close(self):
        if self.busy:
            messagebox.showinfo('正在操作', '正在检查或写入文件，请等待本次操作完成后关闭。', parent=self.root)
            return
        self.executor.shutdown(wait=False, cancel_futures=True)
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--probe', action='store_true', help='Read-only automatic discovery and compatibility check')
    parser.add_argument('--self-test', type=Path, metavar='NEW_DIRECTORY', help='Test only inside a new isolated directory')
    parser.add_argument('--tmp-root', type=Path, help='Read-only source for --self-test, or directory for --probe')
    args = parser.parse_args()
    if args.probe or args.self_test:
        try:
            if args.self_test:
                from installer_selftest import run
                result = run(args.self_test, args.tmp_root, args.assets)
                # Exercise the real bundled Tcl/Tk runtime without showing a window
                # or invoking an installation against the user's real directories.
                root = tk.Tk()
                root.withdraw()
                app = InstallerApp(root, args.assets, scan=False)
                root.update_idletasks()
                app.licenses()
                root.update_idletasks()
                result['gui_runtime'] = str(root.tk.call('info', 'patchlevel'))
                result['license_viewer'] = any(isinstance(widget, tk.Toplevel) for widget in root.winfo_children())
                app.close()
            else:
                fonts = backend.bundled_fonts(args.assets)
                paths, notes = backend.discover()
                if args.tmp_root:
                    paths = [args.tmp_root]
                result = dict(version=backend.VERSION, fonts=len(fonts), candidates=len(paths), notes=notes,
                              installations=[backend.inspect(path) for path in paths], game_started=False)
            code = 0
        except Exception as exc:
            result, code = dict(passed=False, error=backend.friendly_error(exc), detail=str(exc)), 1
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            atomic_json(args.report, result)
        if sys.stdout:
            print(json.dumps(result, ensure_ascii=False, indent=2))
        raise SystemExit(code)
    if os.name == 'nt':
        import ctypes
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
    root = tk.Tk()
    InstallerApp(root, args.assets)
    root.mainloop()


if __name__ == '__main__':
    main()
