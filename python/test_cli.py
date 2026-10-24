"""Проверки консольного интерфейса и BAT на Windows с русскими путями."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

BASE = Path(__file__).resolve().parent
ROOT = BASE.parent
EXE = os.environ.get('TABLE2FBD_TEST_EXE')


class ConsoleSupport:
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='Таблица FBD ')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.source = self.folder/'Исходные данные'/'Режимы вентилятора.xlsx'
        self.source.parent.mkdir()
        shutil.copyfile(ROOT/'assets/Template.xlsx', self.source)

    def run_cli(self, *args, cwd=None):
        command = [str(Path(EXE).resolve())] if EXE else [sys.executable, str(BASE/'cli.py')]
        return subprocess.run(command+list(map(str,args)), cwd=cwd or self.folder,
                              capture_output=True, text=True, encoding='utf-8', timeout=60)

    def read_macro(self, path):
        with zipfile.ZipFile(path) as z:
            self.assertIsNone(z.testzip())
            documents = json.loads(z.read('Template'))
            metadata = json.loads(z.read('MetaData'))
        self.assertIn(metadata['ComponentMetaData']['UniqueId'], documents)
        return documents, metadata

class ConsoleTests(ConsoleSupport, unittest.TestCase):
    def test_default_output_beside_input_with_same_name(self):
        original = self.source.read_bytes()
        result = self.run_cli(self.source)
        self.assertEqual(result.returncode, 0, result.stderr)
        target = self.source.with_suffix('.tple')
        documents, _ = self.read_macro(target)
        self.assertEqual(len(documents), 4)
        self.assertIn(str(target), result.stdout)
        self.assertIn('сводный макрос', result.stdout)
        self.assertEqual(self.source.read_bytes(), original)
        self.assertEqual(result.stderr, '')

    def test_relative_input_and_explicit_output(self):
        caller = self.folder/'Папка запуска';caller.mkdir()
        result = self.run_cli('--generate', '../Исходные данные/Режимы вентилятора.xlsx',
                              '--output', '../Результаты/Готовый макрос.tple', '--name', 'Режимы',
                              '--sheet', 'Данные', '--step', 7, cwd=caller)
        self.assertEqual(result.returncode, 0, result.stderr)
        documents, metadata = self.read_macro(self.folder/'Результаты/Готовый макрос.tple')
        self.assertEqual(metadata['ComponentMetaData']['Name'], 'Режимы')
        self.assertEqual(len(documents), 4)
        self.assertFalse(self.source.with_suffix('.tple').exists())

    def test_single_and_argument_alias(self):
        result = self.run_cli('--generate', self.source, '-o', 'Плоский.tple', '--single')
        self.assertEqual(result.returncode, 0, result.stderr)
        documents, _ = self.read_macro(self.folder/'Плоский.tple')
        self.assertEqual(len(documents), 1)

    def test_empty_output_uses_default(self):
        result = self.run_cli('--generate', self.source, '--output', '', '--sheet', '')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.read_macro(self.source.with_suffix('.tple'))

    def test_input_error_nonzero_and_preserves_existing_macro(self):
        target = self.folder/'Прежний.tple';target.write_bytes(b'previous')
        result = self.run_cli('Нет такого файла.xlsx', '--output', target)
        self.assertEqual(result.returncode, 1)
        self.assertIn('Ошибка:', result.stderr)
        self.assertNotIn('Traceback', result.stderr)
        self.assertEqual(target.read_bytes(), b'previous')
        self.assertEqual(result.stdout, '')

    def test_invalid_step_and_output_suffix(self):
        for arguments in [('--step','0'),('--output','result.xlsx')]:
            with self.subTest(arguments=arguments):
                result = self.run_cli(self.source,*arguments)
                self.assertEqual(result.returncode,1)
                self.assertIn('Ошибка:',result.stderr)
                self.assertFalse(self.source.with_suffix('.tple').exists())

    def test_usage_errors(self):
        for arguments in [(),(self.source,'--generate',self.source),(self.source,'--single','--step','7')]:
            with self.subTest(arguments=arguments):
                result = self.run_cli(*arguments)
                self.assertEqual(result.returncode,2)
                self.assertFalse(self.source.with_suffix('.tple').exists())

    def test_help_and_version_without_input(self):
        for option in ('--help','--version'):
            result = self.run_cli(option)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('Table2FBD',result.stdout)


    def test_default_both_and_explicit_format_with_independent_modes(self):
        result=self.run_cli(self.source,'--fbd-single','--st-step',10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(len(self.read_macro(self.source.with_suffix('.tple'))[0]),1)
        with zipfile.ZipFile(self.source.with_suffix('.fbst')) as z:self.assertEqual(len(json.loads(z.read('Template'))),4)
        result=self.run_cli(self.source,'--st','--st-single','--output','OnlyST.fbst')
        self.assertEqual(result.returncode,0,result.stderr);self.assertFalse((self.folder/'OnlyST.tple').exists())
        with zipfile.ZipFile(self.folder/'OnlyST.fbst') as z:self.assertEqual(len(json.loads(z.read('Template'))),1)
        result=self.run_cli(self.source,'--fbd','--output','OnlyFBD')
        self.assertEqual(result.returncode,0,result.stderr);self.assertFalse((self.folder/'OnlyFBD.fbst').exists())


@unittest.skipUnless(EXE and os.name == 'nt', 'Проверка BAT выполняется после сборки с TABLE2FBD_TEST_EXE.')
class BatchTests(ConsoleSupport, unittest.TestCase):
    def make_batch(self, source, output='', **settings):
        home = self.folder/'Запуск ! & (пакет)';home.mkdir(exist_ok=True)
        executable = home/'Table2FBD-cli.exe'
        if not executable.exists(): shutil.copyfile(EXE, executable)
        text = (ROOT/'Generate.bat').read_text(encoding='utf-8')
        defaults = {'INPUT_FILE':'Template.xlsx','OUTPUT_FILE':'','MACRO_NAME':'TableParameters',
                    'GROUP_STEP':'10','SINGLE_MACRO':'0','ST_GROUP_STEP':'10','ST_SINGLE_MACRO':'0','OUTPUT_FORMAT':'BOTH',
                    'SHEET_NAME':'','PAUSE_AT_END':'0','PAUSE_ON_DROP':'1'}
        values = {'INPUT_FILE':source,'OUTPUT_FILE':output,'PAUSE_ON_DROP':'0',**settings}
        for key,value in values.items():
            text = text.replace(f'set "{key}={defaults[key]}"',f'set "{key}={str(value).replace("%","%%")}"')
        batch = home/'Сформировать макрос.bat'
        batch.write_bytes(text.replace('\r\n','\n').replace('\n','\r\n').encode('utf-8'))
        return batch

    def run_batch(self,batch,*args):
        caller = self.folder/'Другая текущая папка';caller.mkdir(exist_ok=True)
        # Не раскрываем ! уже в имени запускаемого BAT; внутри он также задаёт DisableDelayedExpansion.
        arguments=''.join(f' "{value}"' for value in args)
        command = f'"{os.environ.get("COMSPEC", "cmd.exe")}" /d /v:off /s /c ""{batch}"{arguments}"'
        return subprocess.run(command,cwd=caller,capture_output=True,text=True,encoding='utf-8',timeout=60)

    def test_bat_relative_input_default_output(self):
        batch = self.make_batch(r'..\Исходные данные\Режимы вентилятора.xlsx',SHEET_NAME='Данные')
        result = self.run_batch(batch)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.read_macro(self.source.with_suffix('.tple'))
        self.assertIn(str(self.source.with_suffix('.tple')),result.stdout)

    def test_bat_explicit_relative_output_with_special_characters(self):
        source = self.source.parent/'Режимы ! & (25%).xlsx';shutil.copyfile(self.source,source)
        batch = self.make_batch('../Исходные данные/'+source.name,
            '../Результаты ! & (проверка)/Готовый макрос.tple',MACRO_NAME='Режимы',SINGLE_MACRO='1')
        result = self.run_batch(batch)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        documents, metadata = self.read_macro(self.folder/'Результаты ! & (проверка)/Готовый макрос.tple')
        self.assertEqual(len(documents),1)
        self.assertEqual(metadata['ComponentMetaData']['Name'],'Режимы')

    def test_bat_absolute_paths(self):
        target = self.folder/'Абсолютный путь'/'Результат.tple'
        result = self.run_batch(self.make_batch(str(self.source),str(target)))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.read_macro(target)

    def test_bat_failure_returns_code_and_keeps_file(self):
        target = self.folder/'Прежний.tple';target.write_bytes(b'previous')
        result = self.run_batch(self.make_batch('Отсутствует.xlsx','../Прежний.tple'))
        self.assertEqual(result.returncode,1)
        self.assertIn('Ошибка:',result.stderr)
        self.assertEqual(target.read_bytes(),b'previous')

    def test_bat_dropped_file_overrides_configured_input(self):
        source=self.source.parent/'Перетянутый ! & (25%).xlsx';shutil.copyfile(self.source,source)
        batch=self.make_batch('Не использовать.xlsx')
        result=self.run_batch(batch,source)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        documents,_=self.read_macro(source.with_suffix('.tple'))
        self.assertEqual(len(documents),4)
        self.assertFalse(self.source.with_suffix('.tple').exists())

    def test_bat_relative_argument_uses_callers_directory(self):
        target=self.folder/'Готовые макросы'/'Результат.tple'
        batch=self.make_batch('Не использовать.xlsx','../Готовые макросы/Результат.tple',SINGLE_MACRO='1')
        result=self.run_batch(batch,'../Исходные данные/Режимы вентилятора.xlsx')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        documents,_=self.read_macro(target);self.assertEqual(len(documents),1)
        self.assertFalse(self.source.with_suffix('.tple').exists())

    def test_bat_rejects_multiple_dropped_files(self):
        batch=self.make_batch(str(self.source))
        result=self.run_batch(batch,self.source,self.source)
        self.assertEqual(result.returncode,2,result.stdout+result.stderr)
        self.assertIn('один файл',result.stderr)
        self.assertFalse(self.source.with_suffix('.tple').exists())

    def test_bat_missing_dropped_file_does_not_use_configured_input(self):
        batch=self.make_batch(str(self.source))
        result=self.run_batch(batch,self.source.parent/'Нет файла.xlsx')
        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
        self.assertFalse(self.source.with_suffix('.tple').exists())


    def test_bat_explicit_st_key_and_independent_mode(self):
        batch=self.make_batch('Не использовать.xlsx',ST_SINGLE_MACRO='1')
        result=self.run_batch(batch,self.source,'--st')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertFalse(self.source.with_suffix('.tple').exists())
        with zipfile.ZipFile(self.source.with_suffix('.fbst')) as z:self.assertEqual(len(json.loads(z.read('Template'))),1)


if __name__ == '__main__': unittest.main()
