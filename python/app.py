"""Table2FBD — настольный редактор таблиц и генератор макросов OwenLogic."""
from pathlib import Path
import argparse
import json
import os
import sys
import traceback
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QFontDatabase
from editor_window import MainWindow, Worker, resource
from generation_service import generate, generate_table
from version import VERSION

STYLE = '''
QWidget { font-family: "Segoe UI"; font-size: 9pt; color: #202020; }
QMainWindow { background: #f2f2f2; }
QLineEdit, QComboBox { background: white; border: 1px solid #b8b8b8; padding: 3px 4px; min-height: 18px; }
QLineEdit:disabled { background: #ededed; color: #808080; }
QPushButton, QToolButton { background: #f8f8f8; border: 1px solid #b8b8b8; padding: 4px 9px; }
QPushButton:hover, QToolButton:hover { background: #e6e6e6; border-color: #808080; }
QPushButton:pressed, QToolButton:pressed { background: #d8d8d8; }
QPushButton:disabled, QToolButton:disabled { color: #909090; border-color: #cccccc; }
QTableWidget { background: white; alternate-background-color: #fafafa; border: 1px solid #b8b8b8; gridline-color: #e0e0e0; selection-background-color: #d8e2ec; selection-color: #202020; }
QHeaderView::section { background: #e8e8e8; padding: 3px 5px; border: 0px; border-right: 1px solid #cccccc; border-bottom: 1px solid #cccccc; }
QTabWidget::pane { border: 0px; }
QTabBar::tab { background: #e5e5e5; padding: 5px 16px; border: 1px solid #b8b8b8; }
QTabBar::tab:selected { background: #ffffff; }
QProgressBar { border: 0px; background: #e0e0e0; }
QProgressBar::chunk { background: #707070; }
'''


def main():
    parser = argparse.ArgumentParser(description='Table2FBD — Excel → OwenLogic FBD')
    parser.add_argument('--generate', type=Path); parser.add_argument('--output', type=Path)
    parser.add_argument('--sheet'); parser.add_argument('--name', default='TableParameters')
    parser.add_argument('--step', type=int, default=10); parser.add_argument('--single', action='store_true')
    from cli import add_format_arguments
    add_format_arguments(parser)
    parser.add_argument('--smoke-test', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.generate:
        from cli import generate_from_args
        raise SystemExit(generate_from_args(args))
    if args.smoke_test: os.environ['QT_QPA_PLATFORM'] = 'offscreen'
    app = QApplication(sys.argv[:1]); app.setStyle('Fusion'); app.setStyleSheet(STYLE)
    if args.smoke_test:
        # Модуль offscreen Qt не перечисляет системные шрифты Windows.
        for filename in ('segoeui.ttf', 'seguisb.ttf'):
            QFontDatabase.addApplicationFont(str(Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts'/filename))
    app.setApplicationName('Table2FBD'); app.setApplicationVersion(VERSION)
    window = MainWindow()
    if args.smoke_test:
        destination = args.smoke_test; destination.mkdir(parents=True, exist_ok=True)
        try:
            window.load_file(resource('assets/Template.xlsx'))
            if window.table is None: raise ValueError(window.status.text())
            window.show(); app.processEvents(); window.grab().save(str(destination/'window.png'))
            window.tabs.setCurrentIndex(1); app.processEvents(); window.grab().save(str(destination/'records.png'))
            window.resize(800,480); app.processEvents(); window.grab().save(str(destination/'compact.png'))
            window.data_grid.item(0, 2).setText('37,5')
            window.save_excel(destination/'Edited.xlsx')
            result = generate_table(window.table, destination/'Smoke.tple', name='Smoke')
            result['gui_fields'] = window.field_grid.rowCount(); result['gui_records'] = window.data_grid.rowCount()
            result['version'] = VERSION
            (destination/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        except Exception:
            (destination/'error.txt').write_text(traceback.format_exc(), encoding='utf-8'); raise
        return
    window.show(); sys.exit(app.exec())


if __name__ == '__main__': main()
