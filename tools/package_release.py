"""Архивы исходников Python/C++ и готовой C++-программы для GitHub Releases."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'python'))
from version import VERSION


def sources(names):
    entries=[]
    for name in names:
        relative=PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Недопустимый путь в списке файлов: '+name)
        path=(ROOT/name).resolve()
        if not path.is_relative_to(ROOT) or not path.is_file():
            raise ValueError('Отсутствует разрешённый файл: '+name)
        entries.append((path,'Table2FBD-source/'+name))
    return entries


def pack(path,entries):
    names=[name for _,name in entries]
    if len(names)!=len(set(names)):raise ValueError('Повторяющееся имя в архиве.')
    temporary=path.with_suffix('.zip.tmp')
    try:
        with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for source,name in entries:z.write(source,name)
        with zipfile.ZipFile(temporary) as z:
            if z.testzip() is not None:raise ValueError('Ошибка проверки ZIP.')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    print(f'{path.name}: {path.stat().st_size} байт')
    return f'{digest}  {path.name}'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'release'/VERSION)
    parser.add_argument('--sources-only',action='store_true',help='Только архивы исходников, без готовой программы.')
    args=parser.parse_args()
    output=args.output.resolve()
    manifest=json.loads((ROOT/'release_manifest.json').read_text(encoding='utf-8'))
    sources(manifest['repository'])  # Проверка полноты рабочей копии до создания архивов.
    archives=[(f'Table2FBD-{VERSION}-{name.replace("_","-")}.zip',sources(manifest[name]))
              for name in ('python_source','cpp_source')]
    if not args.sources_only:
        dist=ROOT/'cpp/dist/Table2FBD'
        for name in ('Table2FBD.exe','Table2FBD-cli.exe'):
            if not (dist/name).is_file():raise ValueError('Сначала выполните cpp/build.ps1: нет '+name)
        version=subprocess.run([str(dist/'Table2FBD-cli.exe'),'--version'],
            capture_output=True,text=True,encoding='utf-8',check=True).stdout.strip()
        if version!='Table2FBD C++ '+VERSION:raise ValueError('Версия C++-сборки не совпадает с version.py: '+version)
        subprocess.run([str(dist/'Table2FBD-cli.exe'),str(ROOT/'assets/Template.xlsx'),
            '--output',str(dist/'examples/FanModes'),'--name','FanModes'],check=True)
        mapping={
            'Table2FBD.exe':dist/'Table2FBD.exe','Table2FBD-cli.exe':dist/'Table2FBD-cli.exe',
            'Generate.bat':ROOT/'Generate.bat','Template.xlsx':ROOT/'assets/Template.xlsx',
            'README.txt':ROOT/'docs/USAGE.txt','LICENSE':ROOT/'LICENSE',
            'THIRD_PARTY_NOTICES.md':ROOT/'THIRD_PARTY_NOTICES.md',
            'licenses/json-LICENSE.txt':ROOT/'cpp/vendor/json-LICENSE.txt',
            'licenses/pugixml-LICENSE.txt':ROOT/'cpp/vendor/pugixml-LICENSE.txt',
            'examples/FanModes.tple':dist/'examples/FanModes.tple',
            'examples/FanModes.fbst':dist/'examples/FanModes.fbst'}
        mapping['README.md']=ROOT/'README.md'
        for name in ('BAT.md','USAGE.txt','BUILD.md','RELEASING.md','STRUCTURE.md'):
            mapping['docs/'+name]=ROOT/'docs'/name
        archives.append((f'Table2FBD-{VERSION}-cpp-win32.zip',[(path,'Table2FBD/'+name) for name,path in mapping.items()]))
    output.mkdir(parents=True,exist_ok=True)
    hashes=[pack(output/name,entries) for name,entries in archives]
    (output/'SHA256SUMS.txt').write_text('\n'.join(hashes)+'\n',encoding='utf-8')
    print(output)


if __name__=='__main__':main()
