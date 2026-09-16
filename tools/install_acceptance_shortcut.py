#!/usr/bin/env python3
"""Install a local desktop entry for the guided acceptance window."""
from pathlib import Path
import os
import subprocess

root=Path(__file__).resolve().parents[1]
try:
    desktop=Path(subprocess.check_output(['xdg-user-dir','DESKTOP'],text=True).strip())
except (OSError,subprocess.CalledProcessError):
    desktop=Path.home()/'Desktop'
desktop.mkdir(exist_ok=True)
entry=desktop/'充电桩点按验收.desktop'
def quoted(value):
    return '"'+str(value).replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$')+'"'
entry.write_text('[Desktop Entry]\nVersion=1.0\nType=Application\nName=充电桩点按验收\n'
    'Comment=按步骤操作并解释功能现象\nExec='+quoted(root/'tools/acceptance_gui.sh')+'\n'
    'Icon=dialog-information\nTerminal=false\nCategories=Development;\n')
entry.chmod(0o755)
subprocess.run(['gio','set',str(entry),'metadata::trusted','true'],env=os.environ,check=False)
print(entry)
