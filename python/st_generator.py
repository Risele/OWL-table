"""Макросы OwenLogic ST: CASE с ELSE для неизвестного ID."""
import datetime as dt
import json
import re
import uuid
import zipfile
from xml.sax.saxutils import escape
from pathlib import Path
from excel_to_fbd import NAMESPACE, library_icon, f32

RESERVED = set(('ABS ACTION AND ARRAY AT BOOL BY CASE CONFIGURATION CONSTANT CONTINUE '
    'DATE DINT DO DWORD ELSE ELSIF END END_CASE END_FOR END_FUNCTION END_FUNCTION_BLOCK '
    'END_IF END_PROGRAM END_REPEAT END_STRUCT END_TYPE END_VAR END_WHILE EXIT FALSE '
    'FOR FUNCTION FUNCTION_BLOCK IF INT LINT LREAL LWORD MOD NOT OF OR PROGRAM REAL '
    'REPEAT RETAIN RETURN SINT STRING STRUCT THEN TIME TO TRUE TYPE UDINT UINT ULINT '
    'UNTIL USINT VAR VAR_INPUT VAR_OUTPUT VAR_IN_OUT VAR_TEMP VAR_GLOBAL VAR_EXTERNAL '
    'WHILE WORD XOR').split())
TYPE_NAMES = {0:'BOOL',3:'UDINT',4:'REAL'}


def literal(value, dtype):
    if dtype == 0:return 'TRUE' if value else 'FALSE'
    if dtype == 3:return str(int(value))
    text = format(value, '.9g')
    return text if '.' in text or 'e' in text.lower() else text+'.0'


def build_st(table, name, group_step=10, *, source_name=''):
    if group_step is not None and (type(group_step) is not int or not 1<=group_step<=4294967296):
        raise ValueError('Шаг ST должен быть целым числом от 1 до 4294967296.')
    names = [name,table.input_name]+[n for n,_ in table.outputs]
    for n in names:
        if not n[0].isalpha():raise ValueError(f'ST: имя «{n}» должно начинаться с буквы.')
        if n.upper() in RESERVED:raise ValueError(f'ST: имя «{n}» является ключевым словом языка.')
    if name.casefold() in {n.casefold() for n in names[1:]}:
        raise ValueError('ST: имя макроса должно отличаться от имён его переменных.')
    ns=uuid.uuid5(NAMESPACE,'ST:'+name)
    outputs=table.outputs
    groups=sorted({r['key']//group_step for r in table.rows}) if group_step else []
    now=dt.datetime.now().astimezone()
    created=now.astimezone(dt.timezone.utc).isoformat(timespec='seconds').replace('+00:00','Z')
    description=('Таблица параметров ST. CASE по ID. Неизвестный ID: нулевые выходы, Valid=FALSE. '
        f'Исходный файл: {source_name or "таблица в редакторе (не сохранена)"}. '
        f'Время генерации (местное время ПК): {now:%d.%m.%Y %H:%M:%S}.')
    def doc(tag,value):
        return f'///<{tag}>{escape(" ".join(value.splitlines()))}</{tag}>'
    def title(n):return table.titles.get(n,'Строка существует' if n=='Valid' else n)
    def model(block_name,rows,children=()):
        locals_=[];used={n.casefold() for n in names}
        for i,child in enumerate(children):
            local=f'Group{i}'
            while local.casefold() in used:local='G'+local
            used.add(local.casefold());locals_.append((local,child))
        code=[doc('Description',description),doc('GroupName','Таблицы параметров'),
            f'FUNCTION_BLOCK {block_name}','VAR_INPUT','    '+doc('Description',title(table.input_name)),
            f'    {table.input_name} : UDINT;','END_VAR','VAR_OUTPUT']
        for n,t in outputs:code += ['    '+doc('Description',title(n)),f'    {n} : {TYPE_NAMES[t]};']
        code += ['END_VAR']
        if locals_:
            code += ['VAR']+[f'    {n} : {c["Name"]};' for n,c in locals_]+['END_VAR']
        code += ['',f'CASE {table.input_name} OF']
        if children:
            for group,(local,child) in zip(groups,locals_):
                lo=group*group_step;hi=min(lo+group_step-1,4294967295)
                code += [f'    {lo}..{hi}:',f'        {local}('+', '.join(
                    [f'{table.input_name} := {table.input_name}']+[f'{n} => {n}' for n,_ in outputs])+');']
        else:
            for row in rows:
                comment='; '.join(row['labels'].values())
                comment=' '.join(comment.splitlines())
                if comment:code.append('    // '+comment)
                code.append(f'    {row["key"]}:')
                code += [f'        {n} := {literal(True if n=="Valid" else row["values"][n],t)};' for n,t in outputs]
        code += ['    ELSE']+[f'        {n} := {literal(0,t)};' for n,t in outputs]
        code += ['END_CASE;','END_FUNCTION_BLOCK','']
        source='\r\n'.join(code)
        uid=str(uuid.uuid5(ns,block_name));revision=str(uuid.uuid5(uuid.UUID(uid),'ST-storage-v2:'+source))
        # Обычные переменные и экземпляры блоков хранятся в разных массивах.
        # Индексы обоих массивов относятся к общей последовательности переменных.
        variables=[table.input_name]+[n for n,_ in outputs]
        types=[3]+[6 if t==0 else t for _,t in outputs]
        count=len(variables);start=1+len(locals_)
        return dict(Id=uid,Name=block_name,Password=None,InputNames=[table.input_name],InputDataTypes=[3],
            InputVariableIndexes=[0],OutputNames=[n for n,_ in outputs],OutputDataTypes=types[1:],
            OutputVariableIndexes=list(range(start,start+len(outputs))),SourceCode=source,LastModificationId=revision,
            VariableStorageTypes=[2]*count,VariableDataTypes=types,VariableNames=variables,
            DefaultValues=[None]*count,VariableIndexes=[0]+list(range(start,start+len(outputs))),
            FbInstances=[dict(Name=n,TemplateName=c['Name'],VariableDataType=3) for n,c in locals_],
            FbInstanceIndexes=list(range(1,start)),ArrayRanges=[dict(Start=[],End=[]) for _ in variables],
            ModificationLabel=revision,CreationDateTime=created,Discriminator=2)
    children=[]
    for group in groups:
        lo=group*group_step;hi=min(lo+group_step-1,4294967295)
        child_name=f'{name}_{lo:02d}_{hi:02d}'
        children.append(model(child_name,[r for r in table.rows if r['key']//group_step==group]))
    root=model(name,table.rows,children);docs=[root,*children]
    ins=root['InputNames'];outs=root['OutputNames']
    metadata=dict(Screenshot=library_icon(),MetaData=dict(FunctionBlockUniqueId=root['Id'],
        Description=description,
        Author='',GroupName='Таблицы параметров',VariableNames=ins+outs,
        VariableDescriptions=[table.titles.get(n,'Строка существует' if n=='Valid' else n) for n in ins+outs],Discriminator=2),
        ModificationLabel=root['ModificationLabel'],Name=name,Tags=[],CreationDateTime=created,
        ModificationDateTime=created,HasPassword=False,InputVariableNames=ins,InputVariableDisplayNames=ins,
        InputVariableTypes=[3],OutputVariableNames=outs,OutputVariableDisplayNames=outs,OutputVariableTypes=root['OutputDataTypes'])
    return docs,metadata


def evaluate_st(documents, name, key):
    """Независимое выполнение используемого подмножества ST по сохранённому тексту."""
    by_name={d['Name']:d for d in documents}
    def run(block,depth=0):
        if depth>len(documents):raise ValueError('Цикл вызовов ST.')
        d=by_name[block];source=re.sub(r'//[^\r\n]*','',d['SourceCode'])
        source=re.sub(r'VAR(?:_INPUT|_OUTPUT)?\b.*?END_VAR','',source,flags=re.S|re.I)
        body=re.search(r'FUNCTION_BLOCK\s+\w+\s+(.*?)END_FUNCTION_BLOCK',source,re.S|re.I).group(1)
        result={};types=dict(zip(d['OutputNames'],d['OutputDataTypes']))
        def assignments(text):
            for var,value in re.findall(r'(\w+)\s*:=\s*([^;]+);',text):
                if var not in types:raise ValueError('Неизвестный выход ST: '+var)
                value=value.strip()
                result[var]=(value.upper()=='TRUE' if types[var]==6 else f32(value) if types[var]==4 else int(value))
        prefix,case=body.split('CASE ',1);assignments(prefix)
        selector,branches=case.split(' OF',1)
        if selector.strip()!=d['InputNames'][0]:raise ValueError('Неверный селектор ST.')
        branches=branches.split('END_CASE')[0]
        parts=re.split(r'(?m)^\s*ELSE\s*$',branches,maxsplit=1)
        branches=parts[0];otherwise=parts[1] if len(parts)>1 else ''
        matches=list(re.finditer(r'(?m)^\s*(\d+)(?:\.\.(\d+))?\s*:',branches))
        for i,m in enumerate(matches):
            if int(m[1])<=key<=int(m[2] or m[1]):
                code=branches[m.end():matches[i+1].start() if i+1<len(matches) else len(branches)]
                call=re.fullmatch(r'\s*(\w+)\((.*?)\);\s*',code,re.S)
                if call:
                    instance=next(x for x in d['FbInstances'] if x['Name']==call[1])
                    child=run(instance['TemplateName'],depth+1)
                    if not re.search(r'\b'+re.escape(selector.strip())+r'\s*:=\s*'+re.escape(selector.strip())+r'\b',call[2]):
                        raise ValueError('Не передан ID дочернему ST-блоку.')
                    for child_port,port in re.findall(r'(\w+)\s*=>\s*(\w+)',call[2]):result[port]=child[child_port]
                else:assignments(code)
                break
        else:assignments(otherwise)
        if set(result)!=set(types):raise ValueError('ST не задаёт все выходы на каждом вызове.')
        return result
    return run(name)


def verify_st(documents,table,name):
    by_key={r['key']:r['values'] for r in table.rows}
    keys={0,1,9,10,19,20,4294967295}
    for key in by_key:keys.update(k for k in (key-1,key,key+1) if 0<=k<=4294967295)
    for key in keys:
        expected={n:0 for n,_ in table.outputs}
        if key in by_key:expected.update(by_key[key]);expected['Valid']=True
        if evaluate_st(documents,name,key)!=expected:raise ValueError(f'ST: несовпадение для ID {key}.')
    return len(keys)


def write_st(path,documents,metadata):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('Template',json.dumps(documents,ensure_ascii=False,indent=2))
        z.writestr('MetaData',json.dumps(metadata,ensure_ascii=False,indent=2))
        z.writestr('MinRequiredVersion','2.0')
