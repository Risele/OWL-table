"""Проверки выбора записей и сводного макроса после чтения архива."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from excel_to_fbd import REAL_NAMES, build, f32, write_archive
from table_schema import read_table
from verify_fbd import CompiledDiagram, evaluate, verify
from owen_checks import AVAILABLE, check_fbd_import

BASE=Path(__file__).resolve().parents[1]


class GeneratorTests(unittest.TestCase):
    @unittest.skipUnless(AVAILABLE,'Требуются установленная OwenLogic и .NET SDK.')
    def test_owen_load_store_reload_and_library_element(self):
        table=read_table(BASE/'assets/Template.xlsx')
        with tempfile.TemporaryDirectory(prefix='FBD импорт ') as folder:
            for step in (None,10):
                root,docs=build(table.rows,'Параметры',outputs=table.outputs,input_name=table.input_name,group_step=step)
                path=Path(folder)/'Параметры.tple';write_archive(path,root,docs)
                result=check_fbd_import(path)
                self.assertIn('LOADED: Параметры\n',result)
                self.assertEqual(result.count('STORED'),len(docs))
                self.assertEqual(result.count('LIBRARY_ELEMENT_CREATED'),len(docs))

    def test_group_archive_contains_summary_with_all_groups(self):
        table=read_table(BASE/'assets/Template.xlsx')
        root,docs=build(table.rows,'FanModes',outputs=table.outputs,input_name=table.input_name)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'FanModes.tple';write_archive(path,root,docs)
            with zipfile.ZipFile(path) as z:
                self.assertIsNone(z.testzip())
                loaded=json.loads(z.read('Template'));metadata=json.loads(z.read('MetaData'))
                self.assertNotIn('SourceCode',z.read('Template').decode())
            uid=metadata['ComponentMetaData']['UniqueId']
            self.assertEqual(metadata['ComponentMetaData']['Name'],'FanModes')
            self.assertIn(uid,loaded)
            summary=loaded[uid]['DocumentModel']['Elements']
            groups={e['MacroUniqueId'] for e in summary if e['Discriminator']==37}
            self.assertEqual(groups,set(loaded)-{uid})
            self.assertEqual(len(groups),3)
            self.assertEqual(sum(e['Discriminator']==38 for e in summary),1)
            graph=CompiledDiagram(loaded,uid)
            for key,speed,reverse in [(0,0,False),(1,25,False),(10,60,False),(20,30,True)]:
                result=graph.evaluate(key)
                self.assertEqual((result['Speed'],result['Reverse'],result['Valid']),(speed,reverse,True))
            for key in (2,9,11,19,21,30,4294967295):
                self.assertTrue(all(v==0 for v in graph.evaluate(key).values()))
            verify(loaded,uid,table.rows)

    def test_fifty_rows_five_groups(self):
        rows=[]
        for key in range(50):
            vals={n:f32((-1 if i%2 else 1)*(key*10+i+.125)) for i,n in enumerate(REAL_NAMES)}
            vals.update(Flag1=bool(key%2),Flag2=bool((key//2)%2))
            rows.append({'key':key,'values':vals,'labels':{'Название':f'Режим {key}'}})
        root,docs=build(rows,'Table50');self.assertEqual(len(docs),6)
        self.assertGreaterEqual(verify(docs,root.uid,rows),52)

    def test_unsigned_key_boundary(self):
        table=read_table(BASE/'assets/Template.xlsx');rows=[]
        for key in (0,9,10,16777216,16777217,4294967295):
            row=copy.deepcopy(table.rows[0]);row['key']=key;row['values']['Reverse']=key==16777217;rows.append(row)
        root,docs=build(rows,outputs=table.outputs);verify(docs,root.uid,rows)
        self.assertFalse(evaluate(docs,root.uid,16777216)['Reverse'])
        self.assertTrue(evaluate(docs,root.uid,16777217)['Reverse'])

    def test_stable_identity_and_changed_revision(self):
        table=read_table(BASE/'assets/Template.xlsx')
        a,da=build(table.rows,outputs=table.outputs);b,db=build(table.rows,outputs=table.outputs)
        self.assertEqual((a.uid,a.modification),(b.uid,b.modification))
        table.rows[0]['values']['Speed']=f32(-.125)
        c,dc=build(table.rows,outputs=table.outputs)
        self.assertEqual(set(da),set(dc));self.assertNotEqual(a.modification,c.modification)
        self.assertEqual(evaluate(dc,c.uid,0)['Speed'],-.125)


if __name__=='__main__':unittest.main()
