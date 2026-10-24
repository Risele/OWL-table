import json
import tempfile
import unittest
import zipfile
import shutil
import subprocess
import datetime as dt
import copy
import xml.etree.ElementTree as ET
from pathlib import Path
from generation_service import generate,generate_table
from table_schema import matrix_table,read_table
from st_generator import evaluate_st,build_st,verify_st

ROOT=Path(__file__).resolve().parents[1]

class StTests(unittest.TestCase):
    def test_else_resets_unknown_id_in_group_and_summary(self):
        table=read_table(ROOT/'assets/Template.xlsx')
        for step in (None,10):
            docs,_=build_st(table,'Режимы',step)
            for doc in docs:
                body=doc['SourceCode'].rsplit('END_VAR',1)[1].lstrip()
                self.assertTrue(body.startswith('CASE '))
                self.assertEqual(body.count('\r\n    ELSE\r\n'),1)
            verify_st(docs,table,'Режимы')
            for key in (1,9,20,99,0,4294967295):
                result=evaluate_st(docs,'Режимы',key)
                if key in (9,99,4294967295):self.assertTrue(all(v==0 for v in result.values()))
                else:self.assertTrue(result['Valid'])
            broken=copy.deepcopy(docs)
            broken[0]['SourceCode']=broken[0]['SourceCode'].replace('        Valid := FALSE;','')
            with self.assertRaisesRegex(ValueError,'не задаёт все выходы'):
                evaluate_st(broken,'Режимы',99)

    def test_documentation_and_library_group(self):
        table=matrix_table([['Номер','ID','Номер'],['Расход < 5 & > 1\nединиц','REAL','Расход']],
            [['0','1'],['10','2']])
        for step in (None,10):
            docs,meta=build_st(table,'Режимы',step,source_name='Режимы & режимы.xlsx')
            created=meta['CreationDateTime']
            self.assertEqual(dt.datetime.fromisoformat(created).utcoffset(),dt.timedelta(0))
            for doc in docs:
                source=doc['SourceCode']
                header=source.split('FUNCTION_BLOCK',1)[0]
                tags=ET.fromstring('<doc>'+header.replace('///','')+'</doc>')
                self.assertEqual(tags.findtext('GroupName'),'Таблицы параметров')
                self.assertEqual(tags.findtext('Description'),meta['MetaData']['Description'])
                self.assertIn('Режимы & режимы.xlsx',tags.findtext('Description'))
                self.assertIn(dt.datetime.fromisoformat(created).astimezone().strftime('%d.%m.%Y %H:%M:%S'),
                    tags.findtext('Description'))
                self.assertEqual(doc['CreationDateTime'],created)
                self.assertIn('Расход &lt; 5 &amp; &gt; 1 единиц',source)
            verify_st(docs,table,'Режимы')
        docs,_=build_st(table,'Режимы',None)
        self.assertIn('таблица в редакторе (не сохранена)',docs[0]['SourceCode'])

    def test_boundaries_values_and_reset_for_missing_keys(self):
        table=matrix_table([['ID','ID','Номер'],['Текст','Комментарий',''],['REAL','REAL','Число'],
            ['UDINT','UDINT','Целое'],['BOOL','BOOL','Флаг']],
            [['0','строка\nEND_CASE; //','-0.125','4294967295','1'],
             ['4294967295','край','3.4028234663852886e38','0','0']])
        for step in (None,10,4294967296):
            docs,meta=build_st(table,'Режимы',step);verify_st(docs,table,'Режимы')
            self.assertEqual(evaluate_st(docs,'Режимы',1),dict(Число=0,Целое=0,Флаг=False,Valid=False))
            self.assertEqual(evaluate_st(docs,'Режимы',0)['Целое'],4294967295)
            self.assertEqual(meta['OutputVariableTypes'],[4,3,6,6])
            self.assertNotIn('\nEND_CASE; //',docs[-1]['SourceCode'])

    def test_formats_and_modes_are_independent(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'Режимы.tple'
            for fbd,st,counts in [(10,None,(4,1)),(None,10,(1,4)),(20,10,(3,4))]:
                result=generate(ROOT/'assets/Template.xlsx',target,group_step=fbd,st_group_step=st)
                self.assertEqual(len(result['results']),2)
                with zipfile.ZipFile(target) as z:self.assertEqual(len(json.loads(z.read('Template'))),counts[0])
                with zipfile.ZipFile(target.with_suffix('.fbst')) as z:self.assertEqual(len(json.loads(z.read('Template'))),counts[1])

    def test_st_error_preserves_both_previous_files(self):
        table=read_table(ROOT/'assets/Template.xlsx')
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'Keep.tple';st=target.with_suffix('.fbst')
            target.write_bytes(b'old fbd');st.write_bytes(b'old st')
            with self.assertRaises(ValueError):generate_table(table,target,name='CASE')
            self.assertEqual(target.read_bytes(),b'old fbd');self.assertEqual(st.read_bytes(),b'old st')
            self.assertEqual(len(list(Path(folder).iterdir())),2)

    def test_group_names_do_not_clash_with_ports(self):
        table=matrix_table([['ID','ID','ID'],['Param','REAL','Group0']],[['0','1']])
        docs,_=build_st(table,'Test',10)
        self.assertEqual(docs[0]['FbInstances'][0]['Name'],'GGroup0');verify_st(docs,table,'Test')

    def test_group_instance_storage_and_global_indexes(self):
        table=read_table(ROOT/'assets/Template.xlsx')
        docs,_=build_st(table,'Test',10)
        for d in docs:
            self.assertTrue(set(d['VariableDataTypes']) <= {3,4,6})
            self.assertTrue(all(i['VariableDataType']==3 for i in d['FbInstances']))
            self.assertTrue(set(d['VariableNames']).isdisjoint(i['Name'] for i in d['FbInstances']))
            indexes=d['VariableIndexes']+d['FbInstanceIndexes']
            self.assertEqual(sorted(indexes),list(range(len(indexes))))
            by_index=dict(zip(d['VariableIndexes'],d['VariableNames']))
            self.assertEqual([by_index[i] for i in d['OutputVariableIndexes']],d['OutputNames'])

    @unittest.skipUnless(shutil.which('pwsh') and Path('C:/Program Files/Owen/Owen Logic/ProgramRelayFBD.dll').exists(),
                         'Для проверки создания блока требуется установленная OwenLogic и PowerShell 7.')
    def test_actual_owen_block_constructor(self):
        with tempfile.TemporaryDirectory(prefix='ST импорт ') as folder:
            for step in (None,10):
                target=Path(folder)/'Режимы.fbst'
                generate(ROOT/'assets/Template.xlsx',target,name='Режимы',formats=('st',),st_group_step=step)
                result=subprocess.run(['pwsh','-NoProfile','-File',str(ROOT/'tools/check_st_import.ps1'),
                    '-Archive',str(target)],capture_output=True,encoding='utf-8',timeout=30)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('OWEN_BLOCK_CREATED',result.stdout)

if __name__=='__main__':unittest.main()
