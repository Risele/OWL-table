"""Консольная генерация FBD без графической библиотеки и без открытия окна."""
import argparse
from pathlib import Path
import sys

from generation_service import generate
from version import VERSION


def message(text, *, error=False):
    stream = sys.stderr if error else sys.stdout
    if stream is not None:
        print(text, file=stream)


def optional_path(value):
    # BAT передаёт пустую настройку как --output "", без сборки строки аргументов.
    return Path(value) if value else None


def generate_from_args(args):
    """Общая операция для консольной программы и прежнего app.py --generate."""
    try:
        source = Path(args.generate).expanduser().resolve()
        output = Path(args.output).expanduser().resolve() if args.output else source.with_suffix('.tple')
        result = generate(source, output, sheet=args.sheet, name=args.name,
                          formats=selected_formats(args),
                          group_step=None if args.single or args.fbd_single or args.fbd_mode=='single' else (args.fbd_step if args.fbd_step is not None else args.step),
                          st_group_step=None if args.single or args.st_single or args.st_mode=='single' else (args.st_step if args.st_step is not None else args.step))
    except Exception as exc:
        message('Ошибка: ' + (str(exc) or type(exc).__name__), error=True)
        return 1
    for item in result['results']:
        message('Создан файл: ' + item['path'])
        message(f"{item['format'].upper()}: {item['name']}; записей: {item['records']}; групп: {item['groups']}; проверено ID: {item['checked']}.")
        if item['groups']:message('На схему следует поместить сводный макрос: ' + item['name'])
    return 0


def selected_formats(args):
    return tuple(f for f in ('fbd','st') if getattr(args,f,False)) or ('fbd','st')


def add_format_arguments(parser):
    parser.add_argument('--fbd',action='store_true',help='создать FBD; без --fbd и --st создаются оба формата')
    parser.add_argument('--st',action='store_true',help='создать ST через CASE')
    for language in ('fbd','st'):
        parser.add_argument('--'+language+'-mode',choices=('grouped','single'),default='grouped',help=language.upper()+': режим генерации')
        mode=parser.add_mutually_exclusive_group()
        mode.add_argument('--'+language+'-single',action='store_true',help=language.upper()+': единый макрос')
        mode.add_argument('--'+language+'-step',type=int,help=language.upper()+': группы с заданным шагом по ID')


def main(argv=None):
    # Одинаковая кодировка при работе в терминале и при перенаправлении в файл.
    for stream in (sys.stdout, sys.stderr):
        if stream is not None and hasattr(stream, 'reconfigure'):
            stream.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(
        description='Table2FBD — генерация макросов OwenLogic из Excel.',
        epilog='Относительные пути отсчитываются от текущей папки запуска. Вывод сообщений: UTF-8.')
    parser.add_argument('input', nargs='?', type=Path, help='исходная книга .xlsx')
    parser.add_argument('--generate', type=Path, help='исходная книга .xlsx вместо позиционного аргумента')
    parser.add_argument('--output', '-o', type=optional_path, help='базовое имя результата; расширения .tple и .fbst определяются форматом')
    parser.add_argument('--name', default='TableParameters', help='имя макроса внутри файла (по умолчанию TableParameters)')
    parser.add_argument('--sheet', help='имя листа Excel')
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--step', type=int, default=10, help='шаг групп по ID (по умолчанию 10)')
    modes.add_argument('--single', action='store_true', help='единый макрос без вложенных групп')
    add_format_arguments(parser)
    parser.add_argument('--version', action='version', version='Table2FBD ' + VERSION)
    args = parser.parse_args(argv)
    if args.single and (args.fbd_step is not None or args.st_step is not None):
        parser.error('--single несовместим с --fbd-step и --st-step; используйте независимые параметры форматов.')
    if args.input is not None and args.generate is not None:
        parser.error('Укажите входной файл один раз: позиционно или через --generate.')
    args.generate = args.generate if args.generate is not None else args.input
    if args.generate is None:
        parser.error('Укажите путь к исходной книге .xlsx.')
    try:
        return generate_from_args(args)
    except KeyboardInterrupt:
        message('Генерация прервана.', error=True)
        return 130


if __name__ == '__main__':
    raise SystemExit(main())
