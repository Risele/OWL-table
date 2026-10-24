"""Структура пользовательской таблицы: названия, типы, имена, затем записи."""
from dataclasses import dataclass
from pathlib import Path
import math
import posixpath
import xml.etree.ElementTree as ET
import zipfile

from excel_to_fbd import NS, f32

TYPES = {'REAL': 4, 'UDINT': 3, 'BOOL': 0}
ALIASES = {'ID': 'ID', 'REAL': 'REAL', 'FLOAT': 'REAL', 'UDINT': 'UDINT', 'BOOL': 'BOOL',
           'КОММЕНТАРИЙ': 'COMMENT', 'COMMENT': 'COMMENT'}


@dataclass(frozen=True)
class Field:
    title: str
    kind: str
    name: str
    column: int


@dataclass
class Table:
    fields: list[Field]
    rows: list[dict]
    sheet: str

    @property
    def input_name(self): return next(f.name for f in self.fields if f.kind == 'ID')

    @property
    def outputs(self): return [(f.name, TYPES[f.kind]) for f in self.fields if f.kind in TYPES] + [('Valid', 0)]

    @property
    def titles(self): return {f.name: f.title for f in self.fields if f.name}


def column_name(index):
    result = ''
    while index:
        index, rem = divmod(index-1, 26); result = chr(65+rem)+result
    return result


def sheet_catalog(z):
    rels = {x.attrib['Id']: x.attrib['Target'] for x in ET.fromstring(z.read('xl/_rels/workbook.xml.rels'))}
    sheets = ET.fromstring(z.read('xl/workbook.xml')).find('s:sheets', NS)
    result = {}
    for sh in sheets:
        target = rels[sh.attrib['{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id']]
        result[sh.attrib['name']] = target.lstrip('/') if target.startswith('/') else posixpath.normpath('xl/'+target)
    return result


def sheet_names(path):
    with zipfile.ZipFile(path) as z: return list(sheet_catalog(z))


def identifier(value, where):
    value = str(value or '').strip()
    if not value or not value.isidentifier() or len(value) > 64:
        raise ValueError(f'{where}: имя должно начинаться с буквы или _, содержать только буквы, цифры и _ (до 64 знаков).')
    return value


def read_cells(path, sheet=None):
    path = Path(path)
    if path.suffix.lower() != '.xlsx': raise ValueError('Выберите файл .xlsx. Старый формат .xls не поддерживается.')
    with zipfile.ZipFile(path) as z:
        catalog = sheet_catalog(z)
        sheet = sheet or ('Данные' if 'Данные' in catalog else next(iter(catalog)))
        if sheet not in catalog: raise ValueError(f'Лист «{sheet}» не найден.')
        strings = []
        if 'xl/sharedStrings.xml' in z.namelist():
            strings = [''.join(t.text or '' for t in e.iterfind('.//s:t', NS))
                       for e in ET.fromstring(z.read('xl/sharedStrings.xml'))]
        cells = {}
        for row in ET.fromstring(z.read(catalog[sheet])).findall('s:sheetData/s:row', NS):
            for c in row:
                a = c.attrib['r']; letters = ''.join(x for x in a if x.isalpha())
                col = 0
                for letter in letters: col = col*26+ord(letter)-64
                rn = int(a[len(letters):]); typ = c.get('t', 'n'); n = c.find('s:v', NS)
                value = n.text if n is not None else None
                if typ == 's' and value is not None: value = strings[int(value)]
                elif typ == 'inlineStr': value = ''.join(t.text or '' for t in c.iterfind('.//s:t', NS))
                elif typ in ('n', 'b') and value is not None: value = float(value)
                formula = c.find('s:f', NS) is not None
                if value is not None or formula: cells[(rn, col)] = (value, formula, typ)
    return cells, sheet


def parse_cells(cells, sheet, *, text_numbers=False):
    def get(row, col):
        value, formula, typ = cells.get((row, col), (None, False, 'n'))
        if formula: raise ValueError(f'{sheet}!{column_name(col)}{row}: замените формулу её вычисленным значением.')
        if typ == 'e': raise ValueError(f'{sheet}!{column_name(col)}{row}: ошибка Excel {value}.')
        return value
    columns = sorted({c for r, c in cells})
    if not columns: raise ValueError('Лист пуст.')
    fields, names = [], {'valid'}
    for col in columns:
        where = f'{sheet}!{column_name(col)}'
        title = str(get(1, col) or '').strip()
        kind_text = str(get(2, col) or '').strip().upper()
        name = str(get(3, col) or '').strip()
        if not title: raise ValueError(f'{where}1: укажите понятное название поля.')
        kind = ALIASES.get(kind_text)
        if kind is None: raise ValueError(f'{where}2: неизвестный тип «{kind_text}». Допустимы ID, Комментарий, REAL, UDINT, BOOL.')
        if kind != 'COMMENT':
            name = identifier(name or ('ID' if kind == 'ID' else ''), where+'3')
            if name.casefold() in names: raise ValueError(f'{where}3: имя «{name}» повторяется или зарезервировано (Valid).')
            names.add(name.casefold())
        elif name:
            raise ValueError(f'{where}3: для комментария оставьте имя выхода пустым.')
        fields.append(Field(title, kind, name, col))
    if sum(f.kind == 'ID' for f in fields) != 1: raise ValueError('Требуется ровно один столбец типа ID.')
    if not any(f.kind in TYPES for f in fields): raise ValueError('Добавьте хотя бы одно поле REAL, UDINT или BOOL.')
    rows, keys = [], set()
    for rownum in sorted({r for r, c in cells if r >= 4}):
        if all(get(rownum, f.column) in (None, '') for f in fields): continue
        key, values, labels = None, {}, {}
        for field in fields:
            value = get(rownum, field.column)
            where = f'{sheet}!{column_name(field.column)}{rownum} ({field.title})'
            if field.kind == 'COMMENT':
                if value is not None and str(value).strip(): labels[field.title+'_'+str(field.column)] = f'{field.title}: {value}'
                continue
            if field.kind == 'BOOL' and isinstance(value, str):
                value = {'TRUE': 1, 'FALSE': 0, 'ИСТИНА': 1, 'ЛОЖЬ': 0}.get(value.strip().upper(), value)
            if text_numbers and isinstance(value, str):
                try: value = float(value.strip().replace(',', '.'))
                except ValueError: pass
            if not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f'{where}: требуется числовое значение' + (' или TRUE/FALSE.' if field.kind == 'BOOL' else '.'))
            if field.kind in ('UDINT', 'ID'):
                if value != int(value) or not 0 <= value <= 4294967295:
                    raise ValueError(f'{where}: требуется целое число от 0 до 4294967295.')
                value = int(value)
            elif field.kind == 'BOOL':
                if value not in (0, 1): raise ValueError(f'{where}: допустимы только 0 и 1.')
                value = bool(value)
            else:
                try: value = f32(value)
                except ValueError as exc: raise ValueError(f'{where}: {exc}') from exc
            if field.kind == 'ID': key = value
            else: values[field.name] = value
        if key in keys: raise ValueError(f'{sheet}, строка {rownum}: ID {key} повторяется.')
        keys.add(key); rows.append({'key': key, 'values': values, 'labels': labels})
    if not rows: raise ValueError('Нет записей. Данные должны начинаться с четвёртой строки.')
    return Table(fields, sorted(rows, key=lambda r: r['key']), sheet)


def read_table(path, sheet=None):
    cells, sheet = read_cells(path, sheet)
    return parse_cells(cells, sheet)


def matrix_table(fields, records, sheet='Данные'):
    """Проверить содержимое редактора; каждое поле задано тройкой заголовков."""
    matrix = list(map(list, zip(*fields))) + records if fields else []
    cells = {(r, c): (v, False, 'n') for r, row in enumerate(matrix, 1)
             for c, v in enumerate(row, 1) if r <= 3 or (v is not None and v != '')}
    return parse_cells(cells, sheet, text_numbers=True)


def read_editor_data(path, sheet=None):
    """Считать даже незавершённую таблицу, чтобы её можно было исправить в окне."""
    cells, sheet = read_cells(path, sheet)
    for (r, c), (v, formula, typ) in cells.items():
        if formula or typ == 'e':
            raise ValueError(f'{sheet}!{column_name(c)}{r}: замените формулу или ошибку Excel значением.')
    columns = sorted({c for r, c in cells})
    def value(r, c):
        v = cells.get((r, c), ('', False, 'n'))[0]
        return '' if v is None else str(int(v)) if isinstance(v, float) and v.is_integer() else str(v)
    fields = [[value(r, c) for r in (1, 2, 3)] for c in columns]
    records = [[value(r, c) for c in columns] for r in sorted({r for r, c in cells if r >= 4})]
    return {'fields': fields, 'records': records}, sheet
