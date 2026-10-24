"""Сохранение таблицы редактора в самостоятельную книгу OOXML без Excel."""
from pathlib import Path
import os
import re
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from table_schema import matrix_table, column_name

S='http://schemas.openxmlformats.org/spreadsheetml/2006/main'
R='http://schemas.openxmlformats.org/officeDocument/2006/relationships'


def write_table(path, fields, records):
    """Создать книгу из одного листа. Сначала проверяются все данные."""
    path=Path(path)
    if path.suffix.lower()!='.xlsx': raise ValueError('Укажите расширение .xlsx.')
    table=matrix_table(fields,records)
    matrix=[list(row) for row in zip(*fields)]
    for row in records:
        if all(v is None or str(v).strip()=='' for v in row): continue
        result=[]
        for field,value in zip(table.fields,row):
            if field.kind=='COMMENT': result.append(str(value or ''));continue
            text=str(value).strip().upper()
            if field.kind=='BOOL':
                result.append(text in ('TRUE','ИСТИНА') or (text not in ('FALSE','ЛОЖЬ') and float(text.replace(',','.'))==1))
            elif field.kind in ('ID','UDINT'): result.append(int(float(text.replace(',','.'))))
            else: result.append(float(text.replace(',','.')))
        matrix.append(result)
    if len(fields)>16384 or len(matrix)>1048576: raise ValueError('Превышен размер листа Excel.')
    ws=ET.Element('worksheet',xmlns=S)
    views=ET.SubElement(ws,'sheetViews');view=ET.SubElement(views,'sheetView',workbookViewId='0')
    ET.SubElement(view,'pane',ySplit='3',topLeftCell='A4',activePane='bottomLeft',state='frozen')
    cols=ET.SubElement(ws,'cols')
    for i,field in enumerate(fields,1):
        ET.SubElement(cols,'col',min=str(i),max=str(i),width=str(min(40,max(18,len(field[0])+2))),customWidth='1')
    body=ET.SubElement(ws,'sheetData')
    for rn,row in enumerate(matrix,1):
        element=ET.SubElement(body,'row',r=str(rn))
        for cn,value in enumerate(row,1):
            cell=ET.SubElement(element,'c',r=f'{column_name(cn)}{rn}',s=str(1 if rn<=3 else 0))
            if isinstance(value,str):
                if re.search(r'[\x00-\x08\x0b\x0c\x0e-\x1f\ud800-\udfff\ufffe\uffff]',value):
                    raise ValueError(f'{column_name(cn)}{rn}: недопустимый символ XML.')
                cell.set('t','inlineStr');t=ET.SubElement(ET.SubElement(cell,'is'),'t')
                t.set('{http://www.w3.org/XML/1998/namespace}space','preserve');t.text=value
            else:
                if isinstance(value,bool):cell.set('t','b');value=int(value)
                ET.SubElement(cell,'v').text=repr(value)
    files={
      '[Content_Types].xml': '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>',
      '_rels/.rels': '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="'+R+'/officeDocument" Target="xl/workbook.xml"/></Relationships>',
      'xl/workbook.xml': '<workbook xmlns="'+S+'" xmlns:r="'+R+'"><sheets><sheet name="Данные" sheetId="1" r:id="rId1"/></sheets></workbook>',
      'xl/_rels/workbook.xml.rels': '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="'+R+'/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="'+R+'/styles" Target="styles.xml"/></Relationships>',
      'xl/styles.xml': '<styleSheet xmlns="'+S+'"><fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/><color rgb="FF202020"/></font></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills><borders count="1"><border/></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>',
      'xl/worksheets/sheet1.xml': ET.tostring(ws,encoding='utf-8',xml_declaration=True)}
    fd,temp=tempfile.mkstemp(suffix='.xlsx',dir=path.parent);os.close(fd)
    try:
        with zipfile.ZipFile(temp,'w',zipfile.ZIP_DEFLATED) as z:
            for name,data in files.items(): z.writestr(name,data)
        os.replace(temp,path)
    finally:
        if os.path.exists(temp):os.unlink(temp)
