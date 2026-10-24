"""Проверка пользовательской структуры Excel и схем с разными типами полей."""
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

from excel_to_fbd import NS, build, model_of
from fbd_service import generate
from table_schema import read_table, column_name, sheet_names
from verify_fbd import CompiledDiagram, verify

BASE = Path(__file__).resolve().parents[1]
TEMPLATE = BASE / 'assets' / 'Template.xlsx'
S = '{' + NS['s'] + '}'


class TableTests(unittest.TestCase):
    def fixture(self, matrix):
        """Заменить данные копии шаблона для проверки повреждённых ячеек."""
        temp = tempfile.TemporaryDirectory(); self.addCleanup(temp.cleanup)
        target = Path(temp.name) / 'input.xlsx'
        with zipfile.ZipFile(TEMPLATE) as src, zipfile.ZipFile(target, 'w') as dst:
            for name in src.namelist():
                data = src.read(name)
                if name == 'xl/worksheets/sheet1.xml':
                    root = ET.fromstring(data); body = root.find('s:sheetData', NS); body.clear()
                    for rn, vals in enumerate(matrix, 1):
                        row = ET.SubElement(body, S+'row', r=str(rn))
                        for cn, value in enumerate(vals, 1):
                            if value is None: continue
                            c = ET.SubElement(row, S+'c', r=column_name(cn)+str(rn))
                            if isinstance(value, tuple):
                                ET.SubElement(c, S+'f').text = value[0]
                                ET.SubElement(c, S+'v').text = str(value[1])
                            elif isinstance(value, str):
                                c.set('t', 'inlineStr'); ET.SubElement(ET.SubElement(c,S+'is'),S+'t').text=value
                            else:
                                if isinstance(value, bool): c.set('t', 'b')
                                ET.SubElement(c, S+'v').text = str(int(value) if isinstance(value,bool) else value)
                    data = ET.tostring(root)
                dst.writestr(name, data)
        return target

    @staticmethod
    def base():
        return [['Номер', 'Описание', 'Коэффициент', 'Счётчик', 'Флаг'],
                ['ID', 'Комментарий', 'REAL', 'UDINT', 'BOOL'],
                ['Номер', None, 'Коэф', 'Count', 'Flag'],
                [0, 'Первая запись', -.125, 4294967295, True],
                [10, 'Вторая запись', 1.25, 16777217, False]]

    def test_neutral_template(self):
        table = read_table(TEMPLATE)
        self.assertEqual(sheet_names(TEMPLATE), ['Данные'])
        self.assertEqual(len(table.outputs), 6)
        self.assertEqual([r['key'] for r in table.rows], [0,1,10,20])
        self.assertEqual(table.rows[-1]['values']['Duration'], 120)

    def test_types_cyrillic_names_and_exact_unsigned_values(self):
        table = read_table(self.fixture(self.base()))
        for step in (None, 1, 7, 10, 4294967296):
            with self.subTest(step=step):
                root, docs = build(table.rows, outputs=table.outputs, input_name=table.input_name, group_step=step)
                verify(docs, root.uid, table.rows)
                graph = CompiledDiagram(docs, root.uid)
                self.assertEqual(graph.evaluate(0), {'Коэф':-.125,'Count':4294967295,'Flag':True,'Valid':True})
                self.assertEqual(graph.evaluate(10)['Count'],16777217)
                self.assertEqual(graph.evaluate(1),dict.fromkeys(['Коэф','Count','Flag','Valid'],0))
                if step is None:
                    self.assertEqual(len(docs),1)
                    self.assertFalse(any(e['Discriminator']==37 for e in docs[root.uid]['DocumentModel']['Elements']))

    def test_ranges_are_based_on_id_not_record_position(self):
        matrix=self.base(); matrix[3][0]=4294967295; matrix[4][0]=9
        table=read_table(self.fixture(matrix))
        root,docs=build(table.rows,'Ranges',outputs=table.outputs,group_step=10)
        names={d['MetaData']['ComponentMetaData']['Name'] for d in docs.values()}
        self.assertEqual(names,{'Ranges','Ranges_00_09','Ranges_4294967290_4294967295'})
        verify(docs,root.uid,table.rows)

    def test_minimal_two_columns_and_single_boolean_output(self):
        matrix=[['Номер','Флаг'],['ID','BOOL'],[None,'Флаг'],[2,'TRUE'],[0,'ЛОЖЬ']]
        table=read_table(self.fixture(matrix))
        self.assertEqual(table.input_name,'ID')
        self.assertEqual([r['key'] for r in table.rows],[0,2])
        root,docs=build(table.rows,outputs=table.outputs,group_step=None)
        verify(docs,root.uid,table.rows)

    def test_arbitrary_number_and_order_of_columns(self):
        matrix=[['Параметр '+str(i) for i in range(35)]+['Номер'],
                ['REAL','BOOL','UDINT']*11+['REAL','BOOL','ID'],
                ['Поле'+str(i) for i in range(35)]+['Ключ'],
                [1.25,True,4294967295]*11+[2.5,False,19]]
        table=read_table(self.fixture(matrix))
        self.assertEqual(len(table.outputs),36)
        root,docs=build(table.rows,outputs=table.outputs,input_name=table.input_name)
        verify(docs,root.uid,table.rows)

    def test_reject_invalid_schema_and_values(self):
        mutations=[(1,0,'UDINT'),(1,1,'ID'),(1,2,'TIME'),(2,2,'Bad name'),
                   (2,2,'Valid'),(2,3,'коэф'),(2,1,'CommentOut'),(0,2,None),
                   (3,0,-1),(3,0,1.5),(3,3,-1),(3,3,4294967296),(3,4,2),
                   (3,2,None),(3,2,1e39),(3,2,'1,25'),(4,0,0),
                   (3,2,('1+1',2)),(2,2,('"Name"','Name'))]
        for rn,cn,value in mutations:
            with self.subTest(cell=column_name(cn+1)+str(rn+1),value=value):
                matrix=self.base(); matrix[rn][cn]=value
                with self.assertRaises(ValueError):read_table(self.fixture(matrix))
        with self.assertRaisesRegex(ValueError,'хотя бы одно'):
            read_table(self.fixture([['Номер','Описание'],['ID','COMMENT'],['ID',None],[0,'Запись']]))
        with self.assertRaisesRegex(ValueError,'Нет записей'):
            read_table(self.fixture(self.base()[:3]))

    def test_reports_cell_address(self):
        matrix=self.base();matrix[3][3]=-1
        with self.assertRaisesRegex(ValueError,'Данные!D4'):
            read_table(self.fixture(matrix))

    def test_blank_rows_comments_and_unsupported_sheet(self):
        matrix=self.base();matrix.insert(4,[None]*5);matrix[3][1]=None
        path=self.fixture(matrix);table=read_table(path)
        self.assertEqual(len(table.rows),2)
        self.assertEqual(table.rows[0]['labels'],{})
        with self.assertRaisesRegex(ValueError,'не найден'):read_table(path,'Несуществующий')

    def test_flat_500_rows_does_not_exhaust_python_stack(self):
        rows=[{'key':i,'values':{'Value':i},'labels':{}} for i in range(500)]
        root,docs=build(rows,outputs=[('Value',3),('Valid',0)],group_step=None)
        self.assertEqual(len(docs),1)
        self.assertGreaterEqual(verify(docs,root.uid,rows),501)

    def test_service_archive_and_preserves_previous_result_on_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            target=Path(temp)/'Result.tple'
            result=generate(TEMPLATE,target,group_step=None)
            self.assertEqual(result['groups'],0);self.assertEqual(result['fields'],5)
            previous=target.read_bytes()
            with zipfile.ZipFile(target) as z:
                self.assertIsNone(z.testzip())
                self.assertEqual(len(json.loads(z.read('Template'))),1)
            matrix=self.base();matrix[3][3]=-1
            with self.assertRaises(ValueError):generate(self.fixture(matrix),target)
            self.assertEqual(target.read_bytes(),previous)
            for step in (0,-1,4294967297):
                with self.assertRaises(ValueError):generate(TEMPLATE,target,group_step=step)
                self.assertEqual(target.read_bytes(),previous)


if __name__=='__main__':unittest.main()
