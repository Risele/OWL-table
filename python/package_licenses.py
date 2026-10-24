"""Добавление сведений о библиотеках в переносимую сборку."""
from importlib import metadata
from pathlib import Path
import shutil
import sys

destination=Path(sys.argv[1])
licenses=destination/'licenses';licenses.mkdir(parents=True,exist_ok=True)
source=Path(__file__).resolve().parents[1]/'assets'/'licenses'
shutil.copytree(source,licenses,dirs_exist_ok=True)
for name in ('PySide6','PySide6_Essentials','PySide6_Addons','shiboken6'):
    distribution=metadata.distribution(name)
    folder=licenses/name;folder.mkdir(exist_ok=True)
    (folder/'METADATA.txt').write_text(distribution.read_text('METADATA'),encoding='utf-8')
    for f in distribution.files:
        if '/licenses/' in str(f).replace('\\','/'):
            shutil.copyfile(distribution.locate_file(f),folder/Path(f).name)
python_license=Path(sys.base_prefix)/'LICENSE.txt'
if python_license.exists():shutil.copyfile(python_license,licenses/'Python-LICENSE.txt')
(destination/'ThirdPartyNotices.txt').write_text('''Table2FBD использует следующие сторонние компоненты:

Python 3.14.6 — Python Software Foundation.
https://www.python.org/downloads/source/

PySide6 / Shiboken6 / Qt 6.11.1 — The Qt Company и участники проекта Qt.
Используются динамические библиотеки. Файлы находятся в _internal.
Исходные пакеты и сведения о лицензиях:
https://download.qt.io/official_releases/QtForPython/pyside6/PySide6-6.11.1-src/
https://download.qt.io/official_releases/qt/6.11/6.11.1/submodules/
https://doc.qt.io/qt-6/licenses-used-in-qt.html

PyInstaller 6.21.0 используется для сборки.
https://github.com/pyinstaller/pyinstaller/tree/v6.21.0

Тексты лицензий и сведения из установленных пакетов находятся в licenses.
Входящие библиотеки поставляются без изменения их исходного кода.
''',encoding='utf-8')
