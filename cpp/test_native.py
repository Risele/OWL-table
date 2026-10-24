"""Сравнение C++ с независимыми проверками формата и вычисления выходов Python."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile
import xml.etree.ElementTree as ET
import datetime as dt
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'python'))
from table_writer import write_table
from table_schema import read_table
from verify_fbd import verify
from st_generator import verify_st
from generation_service import generate
from owen_checks import AVAILABLE, check_fbd_import
EXE=ROOT/'cpp/dist/Table2FBD/Table2FBD-cli.exe'

class NativeTests(unittest.TestCase):
    def run_case(self,fields,rows,fbd_step,st_step):
        with tempfile.TemporaryDirectory(prefix='C++ Таблица ') as folder:
            folder=Path(folder);source=folder/'Параметры.xlsx';write_table(source,fields,rows)
            table=read_table(source);output=folder/'Native.tple'
            args=[str(EXE),str(source),'--output',str(output),'--name','Параметры']
            for fmt,step in [('fbd',fbd_step),('st',st_step)]:
                args += ['--'+fmt+'-step',str(step)] if step else ['--'+fmt+'-single']
            result=subprocess.run(args,capture_output=True,text=True,encoding='utf-8',timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            with zipfile.ZipFile(output) as z:
                docs=json.loads(z.read('Template'));meta=json.loads(z.read('MetaData'))
            verify(docs,meta['ComponentMetaData']['UniqueId'],table.rows)
            if AVAILABLE:
                checked=check_fbd_import(output)
                self.assertEqual(checked.count('LIBRARY_ELEMENT_CREATED'),len(docs))
            with zipfile.ZipFile(output.with_suffix('.fbst')) as z:
                st=json.loads(z.read('Template'));stmeta=json.loads(z.read('MetaData'))
            verify_st(st,table,'Параметры')
            for block in st:
                body=block['SourceCode'].rsplit('END_VAR',1)[1].lstrip()
                self.assertTrue(body.startswith('CASE '))
                self.assertEqual(body.count('\r\n    ELSE\r\n'),1)
                header=block['SourceCode'].split('FUNCTION_BLOCK',1)[0]
                tags=ET.fromstring('<doc>'+header.replace('///','')+'</doc>')
                self.assertEqual(tags.findtext('GroupName'),stmeta['MetaData']['GroupName'])
                self.assertEqual(tags.findtext('Description'),stmeta['MetaData']['Description'])
                self.assertIn(source.name,tags.findtext('Description'))
                self.assertIn(dt.datetime.fromisoformat(stmeta['CreationDateTime']).astimezone().strftime('%d.%m.%Y %H:%M:%S'),
                    tags.findtext('Description'))
                self.assertEqual(block['CreationDateTime'],stmeta['CreationDateTime'])
            expected=generate(source,folder/'Python.tple',name='Параметры',group_step=fbd_step,st_group_step=st_step)
            with zipfile.ZipFile(folder/'Python.tple') as z:pmeta=json.loads(z.read('MetaData'))
            self.assertEqual(meta['ComponentMetaData']['UniqueId'],pmeta['ComponentMetaData']['UniqueId'])
            self.assertEqual(meta['MetaDataInputs'],pmeta['MetaDataInputs'])
            self.assertEqual(meta['MetaDataOutputs'],pmeta['MetaDataOutputs'])
            with zipfile.ZipFile(folder/'Python.fbst') as z:pst=json.loads(z.read('Template'))
            self.assertEqual([d['Id'] for d in st],[d['Id'] for d in pst])
            self.assertEqual([d['VariableDataTypes'] for d in st],[d['VariableDataTypes'] for d in pst])
            self.assertEqual([d['FbInstances'] for d in st],[d['FbInstances'] for d in pst])
            if Path('C:/Program Files/Owen/Owen Logic/ProgramRelayFBD.dll').exists():
                checked=subprocess.run(['pwsh','-NoProfile','-File',str(ROOT/'tools/check_st_import.ps1'),
                    '-Archive',str(output.with_suffix('.fbst'))],capture_output=True,text=True,encoding='utf-8',timeout=30)
                self.assertEqual(checked.returncode,0,checked.stdout+checked.stderr)

    def test_fifty_rows_six_reals_two_bools(self):
        fields=[['Номер','ID','ID'],['Описание','Комментарий','']]
        fields += [[f'Параметр {i}','REAL',f'Параметр{i}'] for i in range(6)]
        fields += [['Разрешён','BOOL','Включить'],['Обратный','BOOL','Реверс']]
        rows=[[str(k),f'Режим {k}']+[str((k-i)*.125) for i in range(6)]+[str(k%2),str(k%3==0).upper()] for k in range(50)]
        self.run_case(fields,rows,10,None)
        self.run_case(fields,rows,None,10)

    def test_unsigned_boundaries_and_distinct_steps(self):
        fields=[['Номер','ID','Номер'],['Выход','UDINT','Значение'],['Дробь','REAL','Дробь'],['Флаг','BOOL','Group0']]
        rows=[['0','4294967295','-0.125','1'],['9','2147483648','1.401298464324817e-45','0'],
            ['20','0','3.4028234663852886e38','1'],['4294967295','17','-1','1']]
        self.run_case(fields,rows,20,10)
        self.run_case(fields,rows,4294967296,4294967296)

    def test_st_validation_does_not_replace_fbd(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);out=folder/'Keep.tple';out.write_bytes(b'previous')
            result=subprocess.run([str(EXE),str(ROOT/'assets/Template.xlsx'),'--output',str(out),'--name','CASE'],capture_output=True)
            self.assertEqual(result.returncode,1);self.assertEqual(out.read_bytes(),b'previous')
            self.assertFalse(out.with_suffix('.fbst').exists())

if __name__=='__main__':unittest.main()
