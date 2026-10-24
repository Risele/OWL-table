"""Редактор полей и записей таблицы для генератора FBD."""
import base64
import copy
from pathlib import Path
import sys
from PySide6.QtCore import QThread, Signal, QUrl, Qt
from PySide6.QtGui import QAction, QDesktopServices, QIcon, QPixmap, QUndoStack, QUndoCommand, QKeySequence, QShortcut
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QLabel, QPushButton, QLineEdit,
    QComboBox, QTableWidget, QTableWidgetItem, QVBoxLayout, QHBoxLayout, QGridLayout, QToolButton,
    QFileDialog, QMessageBox, QTabWidget, QHeaderView, QProgressBar, QStyledItemDelegate, QCheckBox)
from excel_to_fbd import library_icon
from table_schema import matrix_table, read_editor_data, sheet_names, identifier
from table_writer import write_table
from generation_service import generate, generate_table, output_paths
from version import VERSION


def resource(name):
    return Path(getattr(sys,'_MEIPASS',Path(__file__).resolve().parents[1]))/name


class Worker(QThread):
    done=Signal(dict)
    failed=Signal(str)
    def __init__(self,kwargs,parent=None):
        super().__init__(parent);self.kwargs=kwargs
    def run(self):
        try:
            operation=generate_table if 'table' in self.kwargs else generate
            self.done.emit(operation(**self.kwargs))
        except Exception as exc:self.failed.emit(str(exc) or type(exc).__name__)


class FieldTypeDelegate(QStyledItemDelegate):
    def createEditor(self,parent,option,index):
        editor=QComboBox(parent);editor.addItems(['ID','Комментарий','REAL','UDINT','BOOL']);return editor
    def setEditorData(self,editor,index):
        value=index.data();editor.setCurrentText('Комментарий' if value=='COMMENT' else value)
    def setModelData(self,editor,model,index):model.setData(index,editor.currentText())


class EditCommand(QUndoCommand):
    def __init__(self,window,new_state,title):
        super().__init__(title);self.window=window
        self.before=copy.deepcopy(window.state);self.after=copy.deepcopy(new_state)
    def redo(self):self.window.set_state(self.after)
    def undo(self):self.window.set_state(self.before)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__();self.resize(1000,640);self.setMinimumSize(800,480)
        pixmap=QPixmap();pixmap.loadFromData(base64.b64decode(library_icon()));self.setWindowIcon(QIcon(pixmap))
        self.table=None;self.worker=None;self.last_output=None;self.source_path=None;self.loaded_sheet='Данные'
        self.owned_source=False;self.last_dir=str(Path.home()/'Documents');self.rendering=False
        self.state={'fields':[],'records':[]};self.undo=QUndoStack(self)
        body=QWidget();self.setCentralWidget(body);layout=QVBoxLayout(body)
        layout.setContentsMargins(6,6,6,6);layout.setSpacing(4)
        file_menu=self.menuBar().addMenu('Файл');edit_menu=self.menuBar().addMenu('Правка')
        help_menu=self.menuBar().addMenu('Справка')
        def action(menu,text,callback,key=None):
            item=QAction(text,self);item.triggered.connect(callback);menu.addAction(item)
            if key:item.setShortcut(QKeySequence(key))
            return item
        self.new_btn=action(file_menu,'Новая',self.new_document,'Ctrl+N')
        self.open_btn=action(file_menu,'Открыть Excel…',self.choose_source,'Ctrl+O')
        self.save_btn=action(file_menu,'Сохранить в Excel…',self.save_as,'Ctrl+S')
        self.reload_btn=action(file_menu,'Перечитать файл',self.reload)
        file_menu.addSeparator()
        self.template_btn=action(file_menu,'Открыть пример',self.open_example)
        self.folder_btn=action(file_menu,'Открыть папку результата',
            lambda:QDesktopServices.openUrl(QUrl.fromLocalFile(str(Path(self.last_output).parent))))
        self.folder_btn.setEnabled(False)
        self.undo_btn=action(edit_menu,'Отменить',self.undo.undo,'Ctrl+Z')
        self.redo_btn=action(edit_menu,'Повторить',self.undo.redo,'Ctrl+Y')
        self.undo.canUndoChanged.connect(self.undo_btn.setEnabled);self.undo.canRedoChanged.connect(self.redo_btn.setEnabled)
        self.undo_btn.setEnabled(False);self.redo_btn.setEnabled(False)
        action(help_menu,'Формат таблицы и работа с программой',self.help,'F1')
        source=QHBoxLayout();source.setSpacing(4)
        for item,label in [(self.new_btn,'Новая'),(self.open_btn,'Открыть…'),(self.save_btn,'Сохранить…')]:
            button=QToolButton();button.setDefaultAction(item);button.setText(label);source.addWidget(button)
        self.path=QLineEdit();self.path.setReadOnly(True)
        source.addWidget(self.path,1)
        self.sheet_label=QLabel('Лист:');source.addWidget(self.sheet_label)
        self.sheet=QComboBox();self.sheet.setMinimumWidth(110);self.sheet.setMaximumWidth(220)
        self.sheet.currentTextChanged.connect(self.switch_sheet);source.addWidget(self.sheet);layout.addLayout(source)
        self.tabs=QTabWidget();self.field_grid=self.make_grid(['Название поля','Тип','Имя входа / выхода'])
        self.field_grid.setItemDelegateForColumn(1,FieldTypeDelegate(self.field_grid))
        self.data_grid=self.make_grid([]);self.edit_buttons=[]
        self.add_tab('Поля',self.field_grid,[('Добавить поле',self.add_field),('Удалить поле',self.remove_fields),
            ('Выше',lambda:self.move_field(-1)),('Ниже',lambda:self.move_field(1))])
        self.add_tab('Записи',self.data_grid,[('Добавить строку',self.add_record),('Копировать строку',self.duplicate_record),
            ('Удалить строки',self.remove_records)])
        layout.addWidget(self.tabs,1)
        for grid in (self.field_grid,self.data_grid):
            grid.itemChanged.connect(self.edited)
            for key,action in [('Ctrl+C',self.copy_cells),('Ctrl+V',self.paste_cells)]:
                shortcut=QShortcut(QKeySequence(key),grid);shortcut.setContext(Qt.ShortcutContext.WidgetShortcut)
                shortcut.activated.connect(lambda g=grid,a=action:a(g))
        settings=QHBoxLayout();settings.setSpacing(6)
        self.name=QLineEdit('TableParameters');self.name.setMaxLength(64)
        self.name.setMinimumWidth(120)
        self.mode=QComboBox();self.mode.addItems(['По группам','Всё в один']);self.mode.setMinimumWidth(120)
        settings.addWidget(QLabel('Макрос:'));settings.addWidget(self.name,1)
        self.fbd_check=QCheckBox('FBD');self.fbd_check.setChecked(True)
        self.step_label=QLabel('Шаг по ID:');self.step=QLineEdit('10');self.step.setFixedWidth(85)
        self.group_hint=QLabel()
        self.generate_btn=QPushButton('Создать макрос…');self.generate_btn.clicked.connect(self.start_generation)
        settings.addWidget(self.generate_btn);layout.addLayout(settings)
        formats=QGridLayout();formats.setHorizontalSpacing(6);formats.setVerticalSpacing(4)
        self.st_check=QCheckBox('ST');self.st_check.setChecked(True)
        self.st_mode=QComboBox();self.st_mode.addItems(['По группам','Всё в один']);self.st_mode.setMinimumWidth(120)
        self.st_step_label=QLabel('Шаг по ID:')
        self.st_step=QLineEdit('10');self.st_step.setFixedWidth(85)
        self.st_group_hint=QLabel()
        for r,widgets in enumerate([(self.fbd_check,self.mode,self.step_label,self.step,self.group_hint),
                (self.st_check,self.st_mode,self.st_step_label,self.st_step,self.st_group_hint)]):
            for c,widget in enumerate(widgets):formats.addWidget(widget,r,c)
        formats.setColumnStretch(5,1);layout.addLayout(formats)
        for signal in (self.fbd_check.toggled,self.st_check.toggled,self.st_mode.currentIndexChanged,self.st_step.textChanged):
            signal.connect(self.update_mode)
        self.mode.currentIndexChanged.connect(self.update_mode);self.step.textChanged.connect(self.update_mode);self.name.textChanged.connect(self.update_mode)
        self.summary=QLabel();layout.addWidget(self.summary)
        self.status=QLabel();self.status.setWordWrap(True);self.status.setObjectName('status');layout.addWidget(self.status)
        self.progress=QProgressBar();self.progress.setRange(0,0);self.progress.setMaximumHeight(5);self.progress.hide();layout.addWidget(self.progress)
        self.undo.cleanChanged.connect(self.update_title);self.new_document()

    def add_tab(self,title,grid,actions):
        widget=QWidget();layout=QVBoxLayout(widget);layout.setContentsMargins(0,4,0,0);layout.setSpacing(4)
        bar=QHBoxLayout()
        for label,action in actions:
            button=QPushButton(label);button.clicked.connect(action);bar.addWidget(button);self.edit_buttons.append(button)
        bar.addStretch();layout.addLayout(bar);layout.addWidget(grid);self.tabs.addTab(widget,title)

    @staticmethod
    def make_grid(headers):
        grid=QTableWidget(0,len(headers));grid.setHorizontalHeaderLabels(headers)
        grid.setAlternatingRowColors(True);grid.verticalHeader().setDefaultSectionSize(22)
        grid.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        grid.horizontalHeader().setStretchLastSection(True);return grid

    def update_title(self,*_):
        self.setWindowTitle(f'Table2FBD {VERSION}'+(' *' if not self.undo.isClean() else '')+' — редактор таблиц для OwenLogic')

    def set_state(self,state):
        self.state=copy.deepcopy(state);self.rendering=True
        for grid,rows,headers in [(self.field_grid,state['fields'],['Название поля','Тип','Имя входа / выхода']),
            (self.data_grid,state['records'],[f[2] or f[0] or f'Поле {i+1}' for i,f in enumerate(state['fields'])])]:
            selected=(grid.currentRow(),grid.currentColumn());scroll=(grid.verticalScrollBar().value(),grid.horizontalScrollBar().value())
            grid.setColumnCount(len(headers));grid.setHorizontalHeaderLabels(headers);grid.setRowCount(len(rows))
            for r,row in enumerate(rows):
                for c,value in enumerate(row):
                    item=QTableWidgetItem(str(value));item.setToolTip(str(value));grid.setItem(r,c,item)
            if selected[0]>=0 and selected[1]>=0:grid.setCurrentCell(min(selected[0],len(rows)-1),min(selected[1],len(headers)-1))
            grid.verticalScrollBar().setValue(scroll[0]);grid.horizontalScrollBar().setValue(scroll[1])
        self.rendering=False;self.validate()

    def resized_columns(self):
        for grid in (self.field_grid,self.data_grid):
            grid.resizeColumnsToContents()
            for col in range(grid.columnCount()):grid.setColumnWidth(col,min(300,max(80,grid.columnWidth(col))))

    def edited(self,*_):
        if self.rendering:return
        def rows(grid):return [[grid.item(r,c).text() if grid.item(r,c) else '' for c in range(grid.columnCount())] for r in range(grid.rowCount())]
        self.commit({'fields':rows(self.field_grid),'records':rows(self.data_grid)},'Изменить ячейку')

    def commit(self,state,title):
        if state!=self.state:self.undo.push(EditCommand(self,state,title))

    def validate(self):
        self.table=None
        try:
            self.table=matrix_table(self.state['fields'],self.state['records'])
            self.summary.setText(f'Записей: {len(self.table.rows)} · Полей: {len(self.table.fields)} · Выходов: {len(self.table.outputs)} с Valid')
            self.status.clear();self.status.setStyleSheet('')
        except ValueError as exc:
            self.summary.setText(f"Записей: {len(self.state['records'])} · Полей: {len(self.state['fields'])}")
            self.error(str(exc))
            if str(exc).startswith('Нет записей.'):
                self.status.setText('Нет записей.');self.status.setStyleSheet('')
        self.generate_btn.setEnabled(self.table is not None);self.update_mode()

    def update_mode(self,*_):
        for check,mode,step_edit,label,hint in [(self.fbd_check,self.mode,self.step,self.step_label,self.group_hint),
                (self.st_check,self.st_mode,self.st_step,self.st_step_label,self.st_group_hint)]:
            grouped=mode.currentIndex()==0;enabled=check.isChecked() and self.worker is None
            mode.setEnabled(enabled);step_edit.setEnabled(enabled and grouped)
            step_edit.setVisible(grouped);label.setVisible(grouped);hint.setEnabled(enabled)
            if not grouped:hint.clear();continue
            try:
                step=int(step_edit.text())
                if not 1<=step<=4294967296:raise ValueError()
                groups=len({r['key']//step for r in self.table.rows}) if self.table else 0
                hint.setText(f'Групп: {groups}')
            except ValueError:hint.setText('Неверный шаг')
        self.generate_btn.setEnabled(self.worker is None and self.table is not None and (self.fbd_check.isChecked() or self.st_check.isChecked()))

    def update_sheet_visibility(self):
        visible=self.sheet.count()>1
        self.sheet.setVisible(visible);self.sheet_label.setVisible(visible)

    def confirm_discard(self):
        if self.undo.isClean():return True
        box=QMessageBox(self);box.setWindowTitle('Несохранённая таблица');box.setText('Сохранить изменения таблицы перед продолжением?')
        save=box.addButton('Сохранить…',QMessageBox.ButtonRole.AcceptRole)
        discard=box.addButton('Не сохранять',QMessageBox.ButtonRole.DestructiveRole)
        cancel=box.addButton('Отмена',QMessageBox.ButtonRole.RejectRole);box.setDefaultButton(save);box.setEscapeButton(cancel);box.exec()
        if box.clickedButton() is save:return self.save_as()
        return box.clickedButton() is discard

    def set_document(self,state,path=None,sheets=None,sheet='Данные',owned=False):
        self.source_path=Path(path) if path else None;self.loaded_sheet=sheet;self.owned_source=owned
        self.path.setText(str(path) if path else 'Новая таблица')
        self.sheet.blockSignals(True);self.sheet.clear();self.sheet.addItems(sheets or ['Данные']);self.sheet.setCurrentText(sheet);self.sheet.blockSignals(False)
        self.update_sheet_visibility()
        self.reload_btn.setEnabled(path is not None)
        if path:self.last_dir=str(Path(path).parent)
        self.undo.clear();self.set_state(state);self.undo.setClean();self.update_title();self.resized_columns()

    def new_document(self):
        if not self.confirm_discard():return
        self.set_document({'fields':[['Номер записи','ID','ID'],['Параметр','REAL','Value']],'records':[]})

    def choose_source(self):
        path,_=QFileDialog.getOpenFileName(self,'Открыть таблицу',self.last_dir,'Книги Excel (*.xlsx)')
        if path:self.load_file(path)

    def load_file(self,path):
        if not self.confirm_discard():return False
        try:
            state,sheet=read_editor_data(path);self.set_document(state,path,sheet_names(path),sheet);return True
        except Exception as exc:self.error(str(exc));return False

    def open_example(self):
        if not self.confirm_discard():return
        try:
            state,sheet=read_editor_data(resource('assets/Template.xlsx'));self.set_document(state);self.path.setText('Пример: режимы вентилятора')
        except Exception as exc:self.error(str(exc))

    def switch_sheet(self,sheet):
        if not self.source_path or sheet==self.loaded_sheet:return
        previous=self.loaded_sheet;original_path=self.source_path;owned=self.owned_source
        if self.confirm_discard():
            try:
                state,selected=read_editor_data(original_path,sheet)
                self.set_document(state,original_path,sheet_names(original_path),selected,owned);return
            except Exception as exc:self.error(str(exc))
        self.sheet.blockSignals(True);self.sheet.setCurrentText(previous);self.sheet.blockSignals(False)

    def reload(self):
        if not self.source_path or not self.confirm_discard():return
        try:
            state,sheet=read_editor_data(self.source_path,self.loaded_sheet)
            self.set_document(state,self.source_path,sheet_names(self.source_path),sheet,self.owned_source)
        except Exception as exc:self.error(str(exc))

    def save_as(self):
        filename=(self.source_path.stem+'_edited.xlsx') if self.source_path else 'Table.xlsx'
        path,_=QFileDialog.getSaveFileName(self,'Сохранить текущую таблицу в отдельную книгу',str(Path(self.last_dir)/filename),'Книги Excel (*.xlsx)')
        if not path:return False
        if Path(path).suffix.lower()!='.xlsx':path+='.xlsx'
        try:self.save_excel(path);return True
        except Exception as exc:self.error(str(exc));return False

    def save_excel(self,path):
        path=Path(path)
        if self.source_path and path.resolve()==self.source_path.resolve() and not self.owned_source:
            raise ValueError('Выберите другое имя: сохраняется только текущая таблица, без остальных листов исходной книги.')
        write_table(path,self.state['fields'],self.state['records'])
        self.source_path=path;self.loaded_sheet='Данные';self.owned_source=True;self.last_dir=str(path.parent)
        self.path.setText(str(path));self.sheet.blockSignals(True);self.sheet.clear();self.sheet.addItem('Данные');self.sheet.blockSignals(False)
        self.update_sheet_visibility()
        self.reload_btn.setEnabled(True);self.undo.setClean();self.status.setText('Текущая таблица сохранена: '+str(path));self.status.setStyleSheet('')

    def add_field(self):
        state=copy.deepcopy(self.state);names={f[2].casefold() for f in state['fields']};i=1
        while f'field{i}' in names:i+=1
        state['fields'].append([f'Поле {i}','REAL',f'Field{i}'])
        for row in state['records']:row.append('0')
        self.commit(state,'Добавить поле');self.field_grid.setCurrentCell(len(state['fields'])-1,0)

    def remove_fields(self):
        indexes={i.row() for i in self.field_grid.selectedIndexes()}
        if not indexes:return
        state=copy.deepcopy(self.state)
        state['fields']=[f for i,f in enumerate(state['fields']) if i not in indexes]
        state['records']=[[v for i,v in enumerate(r) if i not in indexes] for r in state['records']]
        self.commit(state,'Удалить поля')

    def move_field(self,direction):
        r=self.field_grid.currentRow();target=r+direction
        if r<0 or not 0<=target<len(self.state['fields']):return
        state=copy.deepcopy(self.state)
        for row in [state['fields'],*state['records']]:row[r],row[target]=row[target],row[r]
        self.commit(state,'Переставить поле');self.field_grid.setCurrentCell(target,0)

    def next_id(self):
        cols=[i for i,f in enumerate(self.state['fields']) if f[1].upper()=='ID'];used=set()
        if cols:
            for row in self.state['records']:
                try:used.add(int(row[cols[0]]))
                except ValueError:pass
        candidate=max(used,default=-1)+1
        if candidate>4294967295:
            candidate=0
            while candidate in used:candidate+=1
        return str(candidate)

    def add_record(self,duplicate=False):
        state=copy.deepcopy(self.state)
        if not state['fields']:self.error('Сначала добавьте поля.');return
        r=self.data_grid.currentRow()
        row=copy.deepcopy(state['records'][r]) if duplicate and r>=0 else ['' if f[1].upper() in ('COMMENT','КОММЕНТАРИЙ') else '0' for f in state['fields']]
        for i,f in enumerate(state['fields']):
            if f[1].upper()=='ID':row[i]=self.next_id()
        state['records'].append(row);self.commit(state,'Копировать строку' if duplicate else 'Добавить строку')
        self.tabs.setCurrentIndex(1);self.data_grid.setCurrentCell(len(state['records'])-1,0)

    def duplicate_record(self):self.add_record(True)

    def remove_records(self):
        indexes={i.row() for i in self.data_grid.selectedIndexes()}
        state=copy.deepcopy(self.state);state['records']=[r for i,r in enumerate(state['records']) if i not in indexes]
        self.commit(state,'Удалить строки')

    def copy_cells(self,grid):
        import csv,io
        indexes=grid.selectedIndexes()
        if not indexes:return
        top,bottom=min(i.row() for i in indexes),max(i.row() for i in indexes)
        left,right=min(i.column() for i in indexes),max(i.column() for i in indexes)
        out=io.StringIO();writer=csv.writer(out,delimiter='\t',lineterminator='\n')
        writer.writerows([[grid.item(r,c).text() if grid.item(r,c) else '' for c in range(left,right+1)] for r in range(top,bottom+1)])
        QApplication.clipboard().setText(out.getvalue())

    def paste_cells(self,grid):
        import csv,io
        try:
            values=list(csv.reader(io.StringIO(QApplication.clipboard().text()),delimiter='\t'))
            if not values:return
            r,c=max(0,grid.currentRow()),max(0,grid.currentColumn());width=grid.columnCount()
            if c+max(map(len,values))>width:raise ValueError('Вставляемые данные выходят за число столбцов. Сначала добавьте поля.')
            state=copy.deepcopy(self.state);key='fields' if grid is self.field_grid else 'records';rows=state[key]
            while len(rows)<r+len(values):
                rows.append(['']*width)
                if key=='fields':
                    for record in state['records']:record.append('')
            for offset,row in enumerate(values):rows[r+offset][c:c+len(row)]=row
            self.commit(state,'Вставить ячейки')
        except Exception as exc:self.error(str(exc))

    def start_generation(self):
        try:
            table=matrix_table(self.state['fields'],self.state['records']);name=identifier(self.name.text(),'Имя макроса')
            step=None if self.mode.currentIndex()==1 or not self.fbd_check.isChecked() else int(self.step.text())
            st_step=None if self.st_mode.currentIndex()==1 or not self.st_check.isChecked() else int(self.st_step.text())
            formats=tuple(f for f,check in [('fbd',self.fbd_check),('st',self.st_check)] if check.isChecked())
            if not formats:raise ValueError('Выберите FBD и/или ST.')
            for fmt,value in [('fbd',step),('st',st_step)]:
                if fmt in formats and value is not None and not 1<=value<=4294967296:raise ValueError('Шаг должен быть от 1 до 4294967296.')
        except ValueError as exc:self.error(str(exc));return
        extension='.tple' if 'fbd' in formats else '.fbst'
        path,_=QFileDialog.getSaveFileName(self,'Сохранить макросы',str(Path(self.last_dir)/(name+extension)),'Макросы OwenLogic (*'+extension+')')
        if not path:return
        if not Path(path).suffix:path+=extension
        additional=[p for p in output_paths(path,formats).values() if p.exists() and p.resolve()!=Path(path).resolve()]
        if additional and QMessageBox.question(self,'Замена файла','Заменить также существующий файл?\n'+ '\n'.join(map(str,additional)))!=QMessageBox.StandardButton.Yes:return
        self.worker=Worker(dict(table=table,target=path,name=name,formats=formats,group_step=step,st_group_step=st_step,
            source_name=self.source_path.name if self.source_path else ''),self)
        self.worker.done.connect(self.generated);self.worker.failed.connect(self.error);self.worker.finished.connect(self.finished)
        self.set_busy(True);self.status.setText('Создание схемы и проверка выходов…');self.status.setStyleSheet('');self.worker.start()

    def set_busy(self,busy):
        for w in [self.new_btn,self.open_btn,self.template_btn,self.save_btn,self.sheet,self.name,self.mode,self.st_mode,self.fbd_check,self.st_check,self.tabs,*self.edit_buttons]:w.setEnabled(not busy)
        self.undo.setActive(not busy)
        self.undo_btn.setEnabled(not busy and self.undo.canUndo());self.redo_btn.setEnabled(not busy and self.undo.canRedo())
        self.reload_btn.setEnabled(not busy and self.source_path is not None)
        self.step.setEnabled(not busy and self.mode.currentIndex()==0);self.generate_btn.setEnabled(not busy and self.table is not None);self.progress.setVisible(busy)
        self.update_mode()

    def generated(self,result):
        self.last_output=result['path'];self.folder_btn.setEnabled(True)
        messages=[]
        for item in result.get('results',[result]):
            text=(f"Сводный макрос: {item['name']}. Групп: {item['groups']}." if item['groups'] else f"Единый макрос: {item['name']}.")
            messages.append(item.get('format','FBD').upper()+': '+text+' '+Path(item['path']).name)
        self.status.setText('\n'.join(messages));self.status.setStyleSheet('')

    def finished(self):
        worker=self.worker;self.worker=None;self.set_busy(False)
        if worker:worker.deleteLater()

    def error(self,message):self.status.setText('Ошибка: '+message);self.status.setStyleSheet('color: #a92d30;')

    def help(self):
        QMessageBox.information(self,'Редактор таблиц',
            'Создайте новую таблицу, откройте пример или загрузите .xlsx.\n\n'
            'На вкладке «Поля» задайте название, тип и имя входа/выхода. На вкладке «Записи» изменяйте значения. '
            'Двойной щелчок начинает редактирование. Тип выбирается из списка.\n\n'
            'Нужен ровно один ID и хотя бы один REAL, UDINT или BOOL. Для комментария имя выхода оставьте пустым. '
            'Имена: буквы, цифры и _. Для ST первый знак — буква; ключевые слова ST запрещены. Valid создаётся автоматически. '
            'Если ID не найден, Valid = FALSE, остальные выходы равны нулю.\n\n'
            'ID и UDINT: 0…4294967295. BOOL: 0/1, TRUE/FALSE. REAL: число с точкой или запятой.\n\n'
            'FBD и ST включаются отдельно. Для каждого формата задаются свой режим и шаг по ID. '
            'Генерация использует текущие данные окна. В режиме групп создаются сводный макрос с выбранным именем '
            'и его группы. На схему ставьте сводный макрос без суффикса диапазона.\n\n'
            '«Сохранить в Excel» создаёт отдельную книгу с текущей таблицей. Строки 1–3: название, тип, имя; '
            'с четвёртой строки — записи. Формулы не поддерживаются.\n\n'
            'Ctrl+Z / Ctrl+Y — отмена / повтор. Ctrl+C / Ctrl+V — обмен прямоугольником ячеек с Excel. '
            'Результаты: .tple для FBD и .fbst для ST. ST использует CASE. '
            'Импорт: OwenLogic → Файл → Импорт из файла.')

    def closeEvent(self,event):
        if self.worker and self.worker.isRunning():
            QMessageBox.information(self,'Создание макроса','Дождитесь завершения генерации.');event.ignore()
        elif self.confirm_discard():event.accept()
        else:event.ignore()
