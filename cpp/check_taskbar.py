"""Проверить кнопку обычного запуска в панели задач Windows; закрыть только своё окно."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import subprocess
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('exe',nargs='?',type=Path,default=Path(__file__).parent/'dist/Table2FBD/Table2FBD.exe')
args=parser.parse_args()
exe=args.exe.resolve()
user=c.WinDLL('user32',use_last_error=True)
callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
user.EnumWindows.argtypes=[callback,w.LPARAM]
user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
user.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
user.GetWindow.argtypes=[w.HWND,w.UINT]
user.GetWindow.restype=w.HWND
user.GetWindowLongW.argtypes=[w.HWND,c.c_int]
user.GetWindowLongW.restype=w.LONG
user.IsWindowVisible.argtypes=[w.HWND]
user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
process=subprocess.Popen([str(exe)],creationflags=0x08000000)
window={}
@callback
def visit(hwnd,_):
    pid=w.DWORD()
    user.GetWindowThreadProcessId(hwnd,c.byref(pid))
    if pid.value!=process.pid or not user.IsWindowVisible(hwnd):return True
    title=c.create_unicode_buffer(256)
    user.GetWindowTextW(hwnd,title,len(title))
    if title.value.startswith('Table2FBD C++'):
        window.update(handle=hwnd,title=title.value,owner=user.GetWindow(hwnd,4),
                      exstyle=user.GetWindowLongW(hwnd,-20)&0xffffffff)
    return True
try:
    deadline=time.monotonic()+10
    while not window and process.poll() is None and time.monotonic()<deadline:
        user.EnumWindows(visit,0)
        time.sleep(.05)
    assert window,'Не найдено главное окно обычного запуска.'
    # Читаем только кнопку проверяемого EXE, без изменения панели задач.
    script=r'''
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$desktop=[System.Windows.Automation.AutomationElement]::RootElement
$condition=[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ClassNameProperty,'Shell_TrayWnd')
$deadline=[DateTime]::UtcNow.AddSeconds(8)
do {
    $taskbar=$desktop.FindFirst([System.Windows.Automation.TreeScope]::Children,$condition)
    if($null -ne $taskbar) {
        $buttons=$taskbar.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)
        foreach($button in $buttons) {
            $item=$button.Current
            if($item.ControlType -eq [System.Windows.Automation.ControlType]::Button -and
               $item.AutomationId.EndsWith($env:TABLE2FBD_TASKBAR_EXE,[StringComparison]::OrdinalIgnoreCase)) {
                [PSCustomObject]@{name=$item.Name;id=$item.AutomationId;offscreen=$item.IsOffscreen} | ConvertTo-Json -Compress
                exit 0
            }
        }
    }
    Start-Sleep -Milliseconds 200
} while([DateTime]::UtcNow -lt $deadline)
Write-Output 'null'
exit 1
'''
    result=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command',script],
        env=dict(os.environ,TABLE2FBD_TASKBAR_EXE=str(exe)),capture_output=True,text=True,
        encoding='utf-8',timeout=20,creationflags=0x08000000)
    button=json.loads(result.stdout.strip() or 'null')
    report=dict(window,taskbar_button=button)
    print(json.dumps(report,ensure_ascii=False))
    assert not window['owner'],'Главное окно имеет владельца.'
    assert window['exstyle']&0x40000,'Нет WS_EX_APPWINDOW.'
    assert not window['exstyle']&0x80,'Установлен WS_EX_TOOLWINDOW.'
    assert result.returncode==0 and button and not button['offscreen'],'Кнопка в панели задач отсутствует.'
finally:
    if window and process.poll() is None:user.PostMessageW(window['handle'],0x0010,0,0)
    try:process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=5)
