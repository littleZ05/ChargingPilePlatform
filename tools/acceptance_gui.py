#!/usr/bin/env python3
"""Click-through acceptance guide; human observations stay separate from machine evidence."""
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import gi

gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib
from acceptance_data import ROOT, DATABASE, prepare, snapshot

STEPS = json.loads((ROOT / 'tools/acceptance_steps.json').read_text())
ENV = dict(os.environ)
ENV.pop('LD_PRELOAD', None)


class Guide(Gtk.Window):
    def __init__(self):
        super().__init__(title='充电桩平台 · 跟着点就能验收')
        self.set_default_size(1160, 820)
        self.set_border_width(14)
        self.connect('destroy', Gtk.main_quit)
        self.results = ['未检查'] * len(STEPS)
        self.baseline = None
        self.index = 0
        self.busy = False
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.add(outer)
        title = Gtk.Label(xalign=0)
        title.set_markup('<span size="x-large" weight="bold">先开窗口 → 按左边顺序操作 → 看现象 → 记录结果</span>')
        outer.pack_start(title, False, False, 0)
        subtitle = Gtk.Label(label='这是操作向导，不是假演示。“符合”只能由你看到实际现象后手动选择。导航已暂缓。', xalign=0)
        outer.pack_start(subtitle, False, False, 0)
        self.buttons = []
        for labels in [[('启动演示窗口', self.launch), ('重开用户端', self.client), ('打开大屏', self.dashboard), ('查看启动日志', self.logs)],
                       [('记下当前数据', lambda: self.read_data(True)), ('读取现在的数据', lambda: self.read_data(False)),
                        ('准备零余额演示用户', lambda: self.fixture('zero')), ('准备闲时折扣场景', lambda: self.fixture('discount'))]]:
            row = Gtk.Box(spacing=8)
            for label, action in labels:
                button = Gtk.Button(label=label)
                button.connect('clicked', lambda _, fn=action: fn())
                row.pack_start(button, True, True, 0)
                self.buttons.append(button)
            outer.pack_start(row, False, False, 0)
        pane = Gtk.Paned.new(Gtk.Orientation.HORIZONTAL)
        outer.pack_start(pane, True, True, 0)
        self.list = Gtk.ListBox()
        self.list.set_selection_mode(Gtk.SelectionMode.SINGLE)
        for index, step in enumerate(STEPS):
            label = Gtk.Label(label=f'{index+1:02d}  {step["title"]}', xalign=0)
            label.set_margin_top(13); label.set_margin_bottom(13)
            self.list.add(label)
        scroll = Gtk.ScrolledWindow(); scroll.add(self.list)
        pane.pack1(scroll, False, False)
        self.text = Gtk.TextView(editable=False, cursor_visible=False, wrap_mode=Gtk.WrapMode.WORD_CHAR)
        self.text.set_left_margin(18); self.text.set_right_margin(16)
        self.text.set_top_margin(12); self.text.set_bottom_margin(12)
        self.text.set_pixels_above_lines(4)
        self.buffer = self.text.get_buffer()
        self.buffer.create_tag('heading', weight=700, scale=1.22, foreground='#185a76')
        right = Gtk.ScrolledWindow(); right.add(self.text); pane.pack2(right, True, False)
        pane.set_position(330)
        verdict = Gtk.Box(spacing=8)
        verdict.pack_start(Gtk.Label(label='这一项我实际看到：'), False, False, 0)
        self.choice = Gtk.ComboBoxText()
        for value in ['未检查', '符合', '不符合', '暂不检查']: self.choice.append_text(value)
        self.choice.set_active(0)
        self.choice.connect('changed', self.record)
        verdict.pack_start(self.choice, False, False, 0)
        for label, action in [('上一项', lambda: self.move(-1)), ('下一项', lambda: self.move(1)),
                              ('打开测试报告', lambda: self.open_file(ROOT/'docs/测试执行结果.md')),
                              ('导出我的验收记录', self.export)]:
            button = Gtk.Button(label=label); button.connect('clicked', lambda _, fn=action: fn())
            verdict.pack_start(button, True, True, 0)
        outer.pack_start(verdict, False, False, 0)
        self.status = Gtk.Label(label='点击“启动演示窗口”，后台登录 admin / 123456；用户手机号建议13800138001。', xalign=0)
        self.status.set_line_wrap(True)
        outer.pack_start(self.status, False, False, 0)
        self.list.connect('row-selected', self.select)
        self.show_all(); self.list.select_row(self.list.get_row_at_index(0))

    def select(self, _, row):
        if row is None: return
        self.index = row.get_index()
        step = STEPS[self.index]
        self.buffer.set_text('')
        for heading, content in [('本项检查', step['title']+'（'+step['requirements']+'）'),
                                 ('① 去哪里，怎么点',step['steps']),('② 应该看到什么',step['expected']),
                                 ('③ 这说明什么',step['meaning']),('④ 什么情况不能算通过',step['failure'])]:
            self.buffer.insert_with_tags_by_name(self.buffer.get_end_iter(),heading+'\n','heading')
            self.buffer.insert(self.buffer.get_end_iter(),content+'\n\n')
        self.choice.set_active(['未检查','符合','不符合','暂不检查'].index(self.results[self.index]))

    def record(self, widget): self.results[self.index] = widget.get_active_text() or '未检查'
    def move(self, delta): self.list.select_row(self.list.get_row_at_index(max(0,min(len(STEPS)-1,self.index+delta))))

    def dialog(self, title, message, confirm=False):
        dialog = Gtk.MessageDialog(transient_for=self, modal=True,
            message_type=Gtk.MessageType.QUESTION if confirm else Gtk.MessageType.INFO,
            buttons=Gtk.ButtonsType.OK_CANCEL if confirm else Gtk.ButtonsType.OK, text=title)
        dialog.format_secondary_text(message)
        result = dialog.run(); dialog.destroy()
        return result == Gtk.ResponseType.OK

    def job(self, description, action):
        if self.busy: return
        self.busy = True
        for button in self.buttons: button.set_sensitive(False)
        self.status.set_text(description)
        def worker():
            try: message = action()
            except Exception as error: message = '未完成：'+str(error)
            GLib.idle_add(done, message)
        def done(message):
            self.busy = False
            for button in self.buttons: button.set_sensitive(True)
            self.status.set_text(message)
            return False
        threading.Thread(target=worker, daemon=True).start()

    def launch(self):
        def run():
            log = ROOT/'build-demo/guide-startup.log'; log.parent.mkdir(exist_ok=True)
            with log.open('w') as output:
                result = subprocess.run([sys.executable,str(ROOT/'tools/run_demo.py'),'--no-map'],
                    cwd=ROOT,env=ENV,stdout=output,stderr=subprocess.STDOUT)
            if result.returncode: raise RuntimeError('启动失败，请点“查看启动日志”。若端口已占用，关闭之前的程序后重试。')
            return '窗口已启动。先在管理员框登录 admin / 123456，再打开大屏；按左侧步骤操作。'
        self.job('正在检查编译并启动窗口，首次可能需要几分钟；请勿重复点击。',run)

    def client(self):
        path=ROOT/'build-verification/src__userclient__userclient/UserClient'
        if not path.exists(): return self.dialog('先启动演示','请先点击启动演示窗口，完成构建。')
        with (ROOT/'build-demo/UserClient.log').open('a') as log:
            subprocess.Popen([str(path)],cwd=path.parent,env=ENV,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        self.status.set_text('用户端已打开。建议先关闭旧用户窗口，登录原手机号恢复订单。')

    def open_file(self, path): subprocess.Popen(['xdg-open',str(path)],env=ENV,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    def dashboard(self): self.open_file(ROOT/'src/webdashboard/index.html')
    def logs(self):
        log=ROOT/'build-demo/guide-startup.log'
        self.dialog('启动日志',log.read_text(errors='replace')[-5500:] if log.exists() else '还没有启动记录。')

    def read_data(self, baseline):
        try: current=snapshot()
        except Exception as error: return self.dialog('暂时读不到数据',str(error))
        if baseline: self.baseline=current
        message=json.dumps(current,ensure_ascii=False,indent=2)
        if not baseline and self.baseline:
            differences={key:round(value-self.baseline[key],2) for key,value in current.items()
                         if isinstance(value,(int,float)) and isinstance(self.baseline.get(key),(int,float))}
            message='相对你记下的数据变化：\n'+json.dumps(differences,ensure_ascii=False,indent=2)+'\n\n现在：\n'+message
        self.dialog('已记下基准' if baseline else '数据库中的实际结果',message+'\n\n这是独立演示库读数，不是自动通过结论；其他人的同时操作也会改变统计。')

    def fixture(self, kind):
        details = ('只把专用演示手机号13800138088的余额设为0。' if kind=='zero' else
                   '准备第一站12小时低负荷演示样本，并调整正常桩的演示阈值；会替换第一站无关联订单的功率样本。')
        if self.dialog('准备演示条件',details+'\n仅修改build-demo独立演示库，不改正式库。你仍需在产品窗口操作并观察结果。',True):
            try: self.dialog('演示条件已准备',prepare(kind))
            except Exception as error: self.dialog('未修改',str(error))

    def export(self):
        folder=ROOT/'build-demo';folder.mkdir(exist_ok=True)
        path=folder/('我的验收记录-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'.md')
        lines=['# 我的点按验收记录','',datetime.datetime.now().isoformat(timespec='seconds'),'',
               '以下为操作者手动记录，不是自动测试结论。导航按组长指示暂缓。','',
               '| 步骤 | 需求 | 观察结论 |','|---|---|---|']
        for step,result in zip(STEPS,self.results):lines.append(f'| {step["title"]} | {step["requirements"]} | {result} |')
        path.write_text('\n'.join(lines)+'\n')
        self.status.set_text('已导出：'+str(path));self.open_file(path)


if __name__=='__main__':
    Guide();Gtk.main()
