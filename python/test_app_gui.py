"""Проверки редактирования, отмены, сохранения и генерации текущих данных."""
import os
os.environ['QT_QPA_PLATFORM']='offscreen'
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import xml.etree.ElementTree as ET
from PySide6.QtCore import QEventLoop,QTimer,Qt
from PySide6.QtWidgets import QApplication,QLineEdit
from PySide6.QtTest import QTest
from app import MainWindow,Worker
from table_schema import read_table,read_editor_data,matrix_table
from table_writer import write_table
from verify_fbd import evaluate

BASE=Path(__file__).resolve().parents[1]
application=QApplication.instance() or QApplication([])


class WindowTests(unittest.TestCase):
    def setUp(self):
        self.window=MainWindow()
        self.addCleanup(self.cleanup_window)
    def cleanup_window(self):
        self.window.undo.setClean();self.window.close()
    def load_example(self):self.window.open_example();return self.window

    def test_new_table_create_and_edit(self):
        w=self.window;self.assertFalse(w.generate_btn.isEnabled())
        w.add_record();self.assertTrue(w.generate_btn.isEnabled())
        w.data_grid.item(0,1).setText('-2,5')
        self.assertEqual(w.table.rows[0]['values']['Value'],-2.5)
        self.assertFalse(w.undo.isClean())
        w.undo.undo();self.assertEqual(w.table.rows[0]['values']['Value'],0)
        w.undo.redo();self.assertEqual(w.table.rows[0]['values']['Value'],-2.5)

    def test_keyboard_edit_commits_into_table(self):
        w=self.load_example();w.show();w.tabs.setCurrentIndex(1);application.processEvents()
        item=w.data_grid.item(1,2);w.data_grid.setCurrentItem(item);w.data_grid.editItem(item)
        editor=w.data_grid.findChild(QLineEdit);self.assertIsNotNone(editor)
        editor.selectAll();QTest.keyClicks(editor,'42.25');QTest.keyClick(editor,Qt.Key.Key_Return)
        application.processEvents();self.assertEqual(w.table.rows[1]['values']['Speed'],42.25)

    def test_sheet_selector_switches_data_and_hides_for_single_sheet(self):
        w=self.window;self.assertTrue(w.sheet.isHidden());self.assertTrue(w.sheet_label.isHidden())
        w.open_example();self.assertTrue(w.sheet.isHidden())
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Несколько листов.xlsx'
            write_table(path,w.state['fields'],w.state['records'])
            with zipfile.ZipFile(path) as z:files={n:z.read(n) for n in z.namelist()}
            s='http://schemas.openxmlformats.org/spreadsheetml/2006/main'
            r='http://schemas.openxmlformats.org/officeDocument/2006/relationships'
            p='http://schemas.openxmlformats.org/package/2006/relationships'
            c='http://schemas.openxmlformats.org/package/2006/content-types'
            book=ET.fromstring(files['xl/workbook.xml'])
            ET.SubElement(book.find('{'+s+'}sheets'),'{'+s+'}sheet',
                {'name':'Резерв','sheetId':'2','{'+r+'}id':'rId3'})
            files['xl/workbook.xml']=ET.tostring(book)
            rels=ET.fromstring(files['xl/_rels/workbook.xml.rels'])
            ET.SubElement(rels,'{'+p+'}Relationship',Id='rId3',Type=r+'/worksheet',Target='worksheets/sheet2.xml')
            files['xl/_rels/workbook.xml.rels']=ET.tostring(rels)
            types=ET.fromstring(files['[Content_Types].xml'])
            ET.SubElement(types,'{'+c+'}Override',PartName='/xl/worksheets/sheet2.xml',
                ContentType='application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml')
            files['[Content_Types].xml']=ET.tostring(types)
            sheet=ET.fromstring(files['xl/worksheets/sheet1.xml'])
            sheet.find('.//{'+s+'}c[@r="C4"]/{'+s+'}v').text='12.5'
            files['xl/worksheets/sheet2.xml']=ET.tostring(sheet)
            with zipfile.ZipFile(path,'w') as z:
                for name,data in files.items():z.writestr(name,data)
            self.assertTrue(w.load_file(path));self.assertFalse(w.sheet.isHidden())
            self.assertFalse(w.sheet_label.isHidden());self.assertEqual(w.sheet.count(),2)
            self.assertEqual(w.table.rows[0]['values']['Speed'],0)
            w.sheet.setCurrentText('Резерв')
            self.assertEqual(w.loaded_sheet,'Резерв');self.assertEqual(w.table.rows[0]['values']['Speed'],12.5)
            w.data_grid.item(0,2).setText('17')
            with patch.object(w,'confirm_discard',return_value=False):w.sheet.setCurrentText('Данные')
            self.assertEqual(w.sheet.currentText(),'Резерв');self.assertEqual(w.table.rows[0]['values']['Speed'],17)
            saved=Path(folder)/'Один лист.xlsx';w.save_excel(saved)
            self.assertTrue(w.sheet.isHidden());self.assertTrue(w.sheet_label.isHidden())
            self.assertEqual(read_table(saved).rows[0]['values']['Speed'],17)

    def test_edit_menu_shortcuts_and_busy_state(self):
        w=self.load_example();w.show();w.tabs.setCurrentIndex(1);w.data_grid.setFocus()
        w.data_grid.item(0,2).setText('42');application.processEvents()
        QTest.keyClick(w.data_grid,Qt.Key.Key_Z,Qt.KeyboardModifier.ControlModifier)
        self.assertEqual(w.table.rows[0]['values']['Speed'],0)
        QTest.keyClick(w.data_grid,Qt.Key.Key_Y,Qt.KeyboardModifier.ControlModifier)
        self.assertEqual(w.table.rows[0]['values']['Speed'],42)
        w.set_busy(True)
        for action in (w.new_btn,w.open_btn,w.save_btn,w.undo_btn,w.redo_btn):self.assertFalse(action.isEnabled())
        w.set_busy(False);self.assertTrue(w.undo_btn.isEnabled())

    def test_rename_move_remove_field_preserves_values_and_undo(self):
        w=self.load_example();w.field_grid.item(2,2).setText('Скорость')
        self.assertEqual(w.table.rows[1]['values']['Скорость'],25)
        w.field_grid.setCurrentCell(2,0);w.move_field(1)
        self.assertEqual(w.state['fields'][3][2],'Скорость')
        self.assertEqual(w.table.rows[1]['values']['Скорость'],25)
        w.field_grid.clearSelection();w.field_grid.selectRow(3);w.remove_fields()
        self.assertNotIn('Скорость',w.table.rows[1]['values'])
        w.undo.undo();self.assertEqual(w.table.rows[1]['values']['Скорость'],25)
        w.add_field();self.assertEqual(w.table.rows[0]['values']['Field1'],0)

    def test_type_change_revalidates_without_discarding_value(self):
        w=self.load_example();w.field_grid.item(2,1).setText('BOOL')
        self.assertIsNone(w.table);self.assertFalse(w.generate_btn.isEnabled())
        self.assertEqual(w.data_grid.item(1,2).text(),'25')
        w.undo.undo();self.assertTrue(w.generate_btn.isEnabled())

    def test_copy_delete_rows_and_duplicate_id(self):
        w=self.load_example();w.data_grid.setCurrentCell(1,0);w.duplicate_record()
        self.assertEqual(w.state['records'][-1][0],'21')
        self.assertEqual(w.table.rows[-1]['values']['Speed'],25)
        w.data_grid.item(4,0).setText('1');self.assertIsNone(w.table)
        w.undo.undo();self.assertIsNotNone(w.table)
        w.data_grid.clearSelection();w.data_grid.selectRow(4);w.remove_records()
        self.assertEqual(len(w.table.rows),4);w.undo.undo();self.assertEqual(len(w.table.rows),5)

    def test_paste_rectangle_and_undo(self):
        w=self.load_example();w.data_grid.setCurrentCell(0,2)
        QApplication.clipboard().setText('12,5\t1,25\n17,5\t2,25')
        w.paste_cells(w.data_grid)
        self.assertEqual(w.table.rows[0]['values']['Speed'],12.5)
        self.assertEqual(w.table.rows[1]['values']['Ramp'],2.25)
        w.undo.undo();self.assertEqual(w.table.rows[0]['values']['Speed'],0)

    def test_save_and_read_preserve_individual_comments_and_types(self):
        w=self.load_example();w.add_field();i=len(w.state['fields'])-1
        w.field_grid.item(i,1).setText('Комментарий');w.field_grid.item(i,2).setText('')
        w.data_grid.item(0,i).setText('=это текст\nвторая строка')
        w.data_grid.item(0,2).setText('-0,125');w.data_grid.item(0,4).setText('4294967295')
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Режимы.xlsx';w.save_excel(path)
            self.assertTrue(w.undo.isClean());loaded=read_table(path)
            self.assertEqual(loaded.rows,w.table.rows)
            data,_=read_editor_data(path)
            self.assertEqual(data['records'][0][i],'=это текст\nвторая строка')
            self.assertEqual(data['records'][0][1],'Остановлен')
            self.assertEqual(loaded.rows[0]['values']['Duration'],4294967295)

    def test_invalid_export_keeps_previous_file(self):
        w=self.load_example()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'Table.xlsx';w.save_excel(path);previous=path.read_bytes()
            w.data_grid.item(0,4).setText('-1')
            with self.assertRaises(ValueError):w.save_excel(path)
            self.assertEqual(path.read_bytes(),previous)
            self.assertFalse(w.undo.isClean())

    def test_cancel_discard_preserves_unsaved_work(self):
        w=self.load_example();w.data_grid.item(0,2).setText('12')
        previous=copy.deepcopy(w.state)
        with patch.object(w,'confirm_discard',return_value=False):
            w.new_document();w.open_example();w.load_file(BASE/'assets/Template.xlsx')
        self.assertEqual(w.state,previous)

    def test_failed_import_preserves_previous_document(self):
        w=self.load_example();previous=copy.deepcopy(w.state)
        self.assertFalse(w.load_file(BASE/'does-not-exist.xlsx'))
        self.assertEqual(w.state,previous)

    def test_generation_uses_unsaved_values_and_summary(self):
        w=self.load_example();w.data_grid.item(0,2).setText('37,5')
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'Modes.tple';w.name.setText('Modes')
            with patch('editor_window.QFileDialog.getSaveFileName',return_value=(str(target),'')):w.start_generation()
            self.assertFalse(w.tabs.isEnabled());self.wait_worker(w.worker)
            self.assertTrue(w.tabs.isEnabled());self.assertFalse(w.undo.isClean())
            with zipfile.ZipFile(target) as z:
                docs=json.loads(z.read('Template'));meta=json.loads(z.read('MetaData'))
            root=meta['ComponentMetaData']['UniqueId']
            self.assertEqual(evaluate(docs,root,0)['Speed'],37.5)
            self.assertEqual(evaluate(docs,root,20)['Reverse'],True)
            self.assertIn('Сводный макрос: Modes',w.status.text())

    def wait_worker(self,worker):
        loop=QEventLoop();timer=QTimer();timer.setSingleShot(True)
        worker.finished.connect(loop.quit);timer.timeout.connect(loop.quit)
        timer.start(30000);loop.exec();timer.stop();application.processEvents()

    def test_worker_failure(self):
        errors=[];worker=Worker(dict(source=BASE/'missing.xlsx',target=BASE/'unused.tple'))
        worker.failed.connect(errors.append);worker.start();self.wait_worker(worker)
        self.assertEqual(len(errors),1)

    def test_independent_format_modes_and_disable_empty_selection(self):
        w=self.load_example();self.assertTrue(w.fbd_check.isChecked());self.assertTrue(w.st_check.isChecked())
        w.mode.setCurrentIndex(1);w.st_mode.setCurrentIndex(0);w.st_step.setText('20')
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'Both.tple'
            with patch('editor_window.QFileDialog.getSaveFileName',return_value=(str(target),'')):w.start_generation()
            self.wait_worker(w.worker)
            with zipfile.ZipFile(target) as z:self.assertEqual(len(json.loads(z.read('Template'))),1)
            with zipfile.ZipFile(target.with_suffix('.fbst')) as z:self.assertEqual(len(json.loads(z.read('Template'))),3)
        w.fbd_check.setChecked(False);w.st_check.setChecked(False);self.assertFalse(w.generate_btn.isEnabled())
        w.st_check.setChecked(True);self.assertTrue(w.generate_btn.isEnabled());self.assertFalse(w.mode.isEnabled())


if __name__=='__main__':unittest.main()
