"""Общая операция генерации для окна программы и командной строки."""
from pathlib import Path
from excel_to_fbd import build, write_archive
from table_schema import identifier, read_table
from verify_fbd import verify


def generate(source, target, *, sheet=None, name='TableParameters', group_step=10):
    source, target = Path(source), Path(target)
    if target.suffix.lower() != '.tple': raise ValueError('Результат должен иметь расширение .tple.')
    if source.resolve() == target.resolve(): raise ValueError('Исходный файл и результат должны различаться.')
    name = identifier(name, 'Имя макроса')
    table = read_table(source, sheet)
    return generate_table(table, target, name=name, group_step=group_step)


def generate_table(table, target, *, name='TableParameters', group_step=10):
    """Создать макрос по проверенному снимку данных редактора."""
    target = Path(target)
    if target.suffix.lower() != '.tple': raise ValueError('Результат должен иметь расширение .tple.')
    name = identifier(name, 'Имя макроса')
    root, documents = build(table.rows, name, outputs=table.outputs, input_name=table.input_name,
                            field_titles=table.titles, group_step=group_step)
    checked = verify(documents, root.uid, table.rows)
    write_archive(target, root, documents)
    return {'path': str(target.resolve()), 'name': name, 'records': len(table.rows),
            'fields': len(table.outputs)-1, 'groups': len(documents)-1, 'root_uid': root.uid,
            'elements': sum(len(d['DocumentModel']['Elements']) for d in documents.values()),
            'checked': checked}
