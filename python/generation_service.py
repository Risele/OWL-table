"""Выбор форматов и подготовка всех результатов до замены файлов."""
from pathlib import Path
import tempfile
import os
from table_schema import identifier,read_table
from fbd_service import generate_table as generate_fbd
from st_generator import build_st,verify_st,write_st


def output_paths(target,formats):
    target=Path(target)
    if target.suffix.lower() not in ('','.tple','.fbst'):raise ValueError('Результат: имя без расширения, .tple или .fbst.')
    if not formats or any(f not in ('fbd','st') for f in formats):raise ValueError('Выберите FBD и/или ST.')
    return {f:target.with_suffix('.tple' if f=='fbd' else '.fbst') for f in dict.fromkeys(formats)}


def generate(source,target,*,sheet=None,**kwargs):
    table=read_table(source,sheet)
    kwargs.setdefault('source_name',Path(source).name)
    return generate_table(table,target,**kwargs)


def generate_table(table,target,*,name='TableParameters',formats=('fbd','st'),group_step=10,st_group_step=10,source_name=''):
    name=identifier(name,'Имя макроса');paths=output_paths(target,formats)
    results=[];staged=[]
    try:
        for fmt,path in paths.items():
            path.parent.mkdir(parents=True,exist_ok=True)
            fd,tmp=tempfile.mkstemp(suffix=path.suffix,dir=path.parent);os.close(fd);tmp=Path(tmp)
            staged.append((tmp,path))
            if fmt=='fbd':result=generate_fbd(table,tmp,name=name,group_step=group_step)
            else:
                docs,meta=build_st(table,name,st_group_step,source_name=source_name);checked=verify_st(docs,table,name)
                write_st(tmp,docs,meta)
                result=dict(name=name,records=len(table.rows),groups=len(docs)-1,checked=checked)
            results.append(dict(result,path=str(path.resolve()),format=fmt))
        # В случае ошибки записи восстанавливаем прежние файлы обоих форматов.
        previous={path:path.read_bytes() if path.exists() else None for _,path in staged}
        replaced=[]
        try:
            for tmp,path in staged:tmp.replace(path);replaced.append(path)
        except Exception:
            for path in replaced:
                if previous[path] is None:path.unlink()
                else:path.write_bytes(previous[path])
            raise
        return dict(results[0],results=results)
    finally:
        for tmp,_ in staged:
            if tmp.exists():tmp.unlink()
