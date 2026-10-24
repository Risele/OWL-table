"""Дополнительная проверка импорта через установленную OwenLogic без её окна."""
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OWEN=Path('C:/Program Files/Owen/Owen Logic')
AVAILABLE=bool(shutil.which('dotnet') and (OWEN/'ProgramRelayFBD.dll').is_file())
_executable=None

def check_fbd_import(path):
    global _executable
    if not AVAILABLE:raise RuntimeError('Требуются OwenLogic и .NET SDK 6 или новее.')
    if _executable is None:
        target=ROOT/'.agents/check_fbd_import'
        subprocess.run(['dotnet','build',str(ROOT/'checks/owen_fbd/owen_fbd.csproj'),
            '--nologo','-o',str(target)],capture_output=True,text=True,encoding='utf-8',check=True,timeout=90)
        _executable=target/'owen_fbd.exe'
    result=subprocess.run([str(_executable),str(Path(path).resolve())],capture_output=True,
                          text=True,encoding='utf-8',timeout=90)
    if result.returncode:raise AssertionError(result.stdout+'\n'+result.stderr)
    return result.stdout

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description='Проверка загрузки и сохранения FBD в OwenLogic без её окна.')
    parser.add_argument('archive');args=parser.parse_args()
    print(check_fbd_import(args.archive))
