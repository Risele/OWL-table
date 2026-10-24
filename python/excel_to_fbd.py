"""Создание макроса OwenLogic FBD из таблицы параметров Excel.

Только стандартная библиотека Python. Исходные книги и проекты не изменяются.
Формат проверен по файлам OwenLogic 3.3.378.0.
"""
from __future__ import annotations

import argparse
import base64
import datetime as dt
import json
import math
from pathlib import Path
import posixpath
import struct
import uuid
import xml.etree.ElementTree as ET
import zipfile
import zlib

REAL_NAMES = tuple(f"Parameter{i}" for i in range(1, 7))
BOOL_NAMES = ("Flag1", "Flag2")
OUTPUTS = [(n, 4) for n in REAL_NAMES] + [(n, 0) for n in BOOL_NAMES] + [("Valid", 0)]
NS = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
ZERO_ID = str(uuid.UUID(int=0))
COLORS = {0: -16777216, 3: -47872, 4: -7722014}
NAMESPACE = uuid.UUID("a2072054-7813-4acc-8513-00f35030cac8")


def f32(value):
    try:
        value = float(value)
        result = struct.unpack("<f", struct.pack("<f", value))[0]
    except (TypeError, ValueError, OverflowError, struct.error) as exc:
        raise ValueError("Ожидается число в диапазоне REAL") from exc
    if not math.isfinite(result) or (value != 0 and result == 0):
        raise ValueError("Число не представимо конечным ненулевым REAL")
    return result


def read_rows(path: Path, sheet_name=None):
    from table_schema import read_table
    return read_table(path, sheet_name).rows


def point(x, y):
    return {"IsEmpty": x == 0 and y == 0, "X": x, "Y": y}


def rectangle(x, y, w, h):
    return {"Location": point(x, y), "Size": {"IsEmpty": False, "Width": w, "Height": h},
            "X": x, "Y": y, "Width": w, "Height": h, "Left": x, "Top": y,
            "Right": x+w, "Bottom": y+h, "IsEmpty": False}


def text_primitive(text, x, y, w, h=8, name="textTitle", align=0):
    return {"Text": str(text), "Name": name, "X": x, "Y": y, "Width": w, "Height": h,
            "TextStyleModel": {"HorzAlign": align, "VertAlign": 0, "TextOffset": point(0, 0),
                               "FontName": "Consolas", "FontSize": 8, "FontStyle": 0,
                               "TextColor": -16777216}, "Discriminator": 2}


def model_of(e):
    return e["ElementModel"] if "ElementModel" in e else e["ElementBlockModel"]["ElementModel"]


class Diagram:
    def __init__(self, name, namespace, description, outputs=None, input_name="TypeId", field_titles=None):
        self.name = name
        self.ns = uuid.uuid5(namespace, name)
        self.uid = str(self.ns)
        self.counter = 0
        self.elements, self.wires = [], []
        self.locations, self.types = {}, {}
        self.description = description
        self.interface = list(outputs if outputs is not None else OUTPUTS)
        self.input_name = input_name
        self.field_titles = field_titles or {}
        self.inputs, self.outputs = [], []
        self.width, self.height = 100, 100

    def new_id(self):
        self.counter += 1
        return str(uuid.uuid5(self.ns, str(self.counter)))

    def block(self, title, fb, discriminator, ins, outs, x, y, width=24, extra=None,
              comment="", in_owners=None, out_owners=None, labels=False):
        uid = self.new_id()
        height = max(10, max(len(ins), len(outs)) * (12 if labels else 5) + (12 if labels else 0))
        bx = x + 2.5
        path = {"Points": [point(bx, y), point(bx+width, y), point(bx+width, y+height), point(bx, y+height)],
                "PointTypes": base64.b64encode(bytes([0, 1, 1, 129])).decode(),
                "StyleSheetName": "WHITE" if discriminator == 29 else "QUESTION", "Discriminator": 1}
        primitives, ports = [path], []
        primitives.append(text_primitive(title, bx, y-10 if labels else y, width,
                                         8 if labels else height, "textIdentificator" if labels else "textTitle"))
        pins = []
        for direction, types, owners in [(0, ins, in_owners), (1, outs, out_owners)]:
            pids = []
            for idx, dtype in enumerate(types):
                pname = ("Input" if direction == 0 else "Output") + str(idx if labels else (idx if direction == 0 else len(ins)+idx))
                py = y + (9+12*idx if labels else (idx+.5)*height/len(types))
                px = x if direction == 0 else bx+width+2.5
                anchor, pid = self.new_id(), self.new_id()
                primitives.append({"LineModel": {"StartPointX": px if direction == 0 else bx+width,
                                                 "StartPointY": py, "EndPointX": bx if direction == 0 else px,
                                                 "EndPointY": py, "Discriminator": 4},
                                   "UniqueId": anchor, "Name": pname, "Color": COLORS[dtype], "Discriminator": 3})
                ports.append({"UniqueId": pid, "AnchorUniqueId": anchor,
                              "ElementOwnerUId": owners[idx] if owners else ZERO_ID,
                              "PortType": direction, "DataType": dtype,
                              "IsFloating": discriminator == 18, "Name": pname,
                              "PointIndexMode": direction, "ArrayType": None})
                self.locations[pid], self.types[pid] = (px, py), dtype
                pids.append(pid)
            pins.append(pids)
        model = {"Version": 0, "UniqueId": uid, "Title": title, "Descriptor": comment,
                 "Location": point(x, y), "IsInvertOutput": False, "FontSize": 8,
                 "Comment": comment, "PropertyNames": [], "OldDisplayNames": [], "NewDisplayNames": [],
                 "FbType": fb, "Primitives": primitives, "Ports": ports,
                 "IsAddToMacroProperty": False, "ZOrder": len(self.elements)+len(self.wires)}
        element = {"ElementModel": model, **(extra or {}), "Discriminator": discriminator}
        self.elements.append(element)
        self.width, self.height = max(self.width, x+width+20), max(self.height, y+height+20)
        return element, pins[0], pins[1]

    def wire(self, source, target):
        if self.types[source] != self.types[target]:
            raise ValueError("Несогласованные типы соединения")
        a, b = self.locations[source], self.locations[target]
        mid = b[0]-7.5
        coords = [a, (mid, a[1]), (mid, b[1]), b]
        self.wires.append({"ConnectorModel": {"UniqueId": self.new_id(), "Color": COLORS[self.types[source]],
                           "Points": [point(*p) for p in coords], "FromPortUId": source, "ToPortUId": target,
                           "ZOrder": len(self.elements)+len(self.wires)}, "Discriminator": 1})

    def input(self, x=0, y=0):
        e, _, out = self.block(self.input_name, 12, 38, [], [3], x, y, width=max(32, len(self.input_name)*3),
                               extra={"Index": 0, "InputName": self.input_name})
        self.inputs.append(e)
        return out[0]

    def output(self, name, dtype, source, x, y):
        e, ins, _ = self.block(name, 13, 39, [dtype], [], x, y, width=max(32, len(name)*3),
                               extra={"Index": len(self.outputs), "OutputName": name},
                               comment=self.field_titles.get(name, name))
        self.outputs.append(e)
        self.wire(source, ins[0])

    def constant(self, value, dtype, x, y, comment=""):
        label = str(int(value)) if dtype in (0, 3) else format(value, ".9g").replace(".", ",")
        e, _, out = self.block(label, 30, 29, [], [dtype], x, y, width=max(24, len(label)*2.5),
                               extra={"Value": int(value) if dtype in (0, 3) else value, "Version": 4}, comment=comment)
        m = model_of(e)
        m["PropertyNames"] = ["Value"]
        m["OldDisplayNames"] = m["NewDisplayNames"] = ["Значение константы"]
        return out[0]

    def compare(self, source, value, x, y, label):
        c = self.constant(value, 3, x, y+15, label)
        e, ins, out = self.block("EQ", 25, 21, [3, 3], [0], x+55, y+10, comment=label)
        model_of(e)["Primitives"].append(text_primitive(label, x, y-8, 125))
        self.wire(source, ins[0]); self.wire(c, ins[1])
        return out[0]

    def select(self, cond, no, yes, x, y):
        dtype = self.types[yes]
        if dtype not in (3, 4): raise ValueError("SEL принимает UDINT или REAL")
        _, ins, out = self.block("fSEL" if dtype == 4 else "SEL", 20, 22 if dtype == 4 else 21,
                                 [0, dtype, dtype], [dtype], x, y)
        for a, b in zip((cond, no, yes), ins): self.wire(a, b)
        return out[0]

    def logic(self, title, a, b, x, y):
        fb, default = (4, 0) if title == "OR" else (5, 1)
        _, ins, out = self.block(title, fb, 18, [0, 0], [0], x, y,
                                 extra={"PinDefaultValue": default, "FbType": fb})
        self.wire(a, ins[0]); self.wire(b, ins[1])
        return out[0]

    def instance(self, child, type_id, x, y):
        width = max(75, max(len(n) for n, _ in self.interface)*3+45, len(child.name)*2.5)
        e, ins, outs = self.block(child.name, -1, 37, [3], [t for _, t in self.interface], x, y,
                                  width=width, labels=True,
                                  in_owners=[model_of(v)["UniqueId"] for v in child.inputs],
                                  out_owners=[model_of(v)["UniqueId"] for v in child.outputs])
        m = e.pop("ElementModel")
        # OwenLogic ищет TextPrimitive при сохранении экземпляра макроса.
        # textIdentificator отдельно отвечает за отображение имени экземпляра.
        m["Primitives"].append(text_primitive(self.input_name, x+4, y+5, 35, name="TextPrimitive"))
        for i, (name, _) in enumerate(self.interface):
            m["Primitives"].append(text_primitive(name, x+42, y+5+12*i, width-42, name="OutputLabel"+str(i)))
        e.update({"ElementBlockModel": {"ElementModel": m, "Identificator": 1},
                  "MacroUniqueId": child.uid, "LastUpdatingId": child.modification,
                  "Properties": {"UniquePropertyNames": [], "PropertyItemModels": []}})
        self.wire(type_id, ins[0])
        return dict(zip((n for n, _ in self.interface), outs))

    def finish(self):
        # Метка меняется вместе с содержимым, идентификатор макроса стабилен.
        content = json.dumps([self.elements, self.wires, self.description], ensure_ascii=False, sort_keys=True)
        self.modification = str(uuid.uuid5(self.ns, content))
        date = int((dt.datetime.now(dt.timezone.utc).timestamp()+62135596800)*10000000) | (1 << 62)
        metadata = {"ComponentMetaData": {"Name": self.name, "UniqueId": self.uid,
                    "ModificationId": self.modification, "GroupName": "Таблицы параметров", "Author": None,
                    "Description": self.description, "Tags": [], "ModificationDateTime": date, "CreationDateTime": date},
                    "HasPassword": False,
                    "MetaDataInputs": [{"Name": self.input_name, "DataType": 3,
                                        "Description": self.field_titles.get(self.input_name, "Номер записи")}],
                    "MetaDataOutputs": [{"Name": n, "DataType": t,
                                         "Description": "Строка существует" if n == "Valid" else self.field_titles.get(n, n)}
                                        for n, t in self.interface]}
        self.store = {"DocumentModel": {"UniqueId": self.uid, "Bounds": rectangle(0, -20, self.width, self.height+20),
                      "ApplicationVersionCreated": "3.3.378.0", "LastChangingApplicationVersion": "3.3.378.0",
                      "Elements": self.elements, "Variables": [], "VariableCatalogSections": [],
                      "ConnectorModels": self.wires, "CommentBlockModels": [], "DevValue": "Macro",
                      "WorkRectangle": rectangle(0, -20, self.width, self.height+20), "KeyFileName": None},
                      "Password": None, "LastUpdatingId": self.modification, "MetaData": metadata, "Discriminator": 2}
        return self


def build(rows, name="TableParameters", *, outputs=None, input_name="TypeId", field_titles=None, group_step=10):
    """group_step=None создаёт единый макрос; положительный шаг задаёт интервалы ID."""
    outputs = list(outputs if outputs is not None else OUTPUTS)
    if not rows: raise ValueError("Таблица не содержит записей")
    if group_step is not None and (type(group_step) is not int or not 1 <= group_step <= 4294967296):
        raise ValueError("Шаг групп должен быть целым числом от 1 до 4294967296")
    if not name.strip(): raise ValueError("Укажите имя макроса")
    field_titles = field_titles or {}
    rows = sorted(rows, key=lambda r: r['key'])
    namespace = uuid.uuid5(NAMESPACE, name)
    children = []
    groups = sorted({r["key"]//group_step for r in rows}) if group_step else [0]
    for group in groups:
        records = [r for r in rows if r["key"]//group_step == group] if group_step else rows
        description = "\n".join(f"{r['key']}: " + "; ".join(r["labels"].values()) for r in records)
        child_name = f"{name}_{group*group_step:02d}_{min(group*group_step+group_step-1, 4294967295):02d}" if group_step else name
        d = Diagram(child_name, namespace, description, outputs, input_name, field_titles)
        type_id = d.input()
        signals = {n: d.constant(0, t, 0, 70+i*30) for i, (n, t) in enumerate(outputs)}
        for col, row in enumerate(records):
            x = 60+col*155
            label = f"№ {row['key']} " + "; ".join(row["labels"].values())
            matched = d.compare(type_id, row["key"], x, 10, label)
            for i, (n, t) in enumerate(outputs):
                y = 70+i*30
                if t in (3, 4):
                    value = d.constant(row["values"][n], t, x, y+12, field_titles.get(n, n))
                    signals[n] = d.select(matched, signals[n], value, x+75, y)
                else:
                    value = d.constant(True if n == "Valid" else row["values"][n], 0, x, y+12, field_titles.get(n, n))
                    selected = d.logic("AND", matched, value, x+50, y)
                    signals[n] = d.logic("OR", signals[n], selected, x+100, y)
        for i, (n, t) in enumerate(outputs): d.output(n, t, signals[n], 70+len(records)*155, 70+i*30)
        children.append(d.finish())
    if group_step is None:
        return children[0], {children[0].uid: children[0].store}
    root = Diagram(name, namespace, f"Сводный макрос таблицы. Автоматический выбор группы. {input_name} — ID. При отсутствии строки Valid=FALSE, параметры равны нулю.",
                   outputs, input_name, field_titles)
    type_id = root.input()
    base_y = max(170, 15+len(outputs)*12+35)
    spacing = max(155, max(len(n) for n, _ in outputs)*3+90, len(name)*2.5+120)
    signals = {n: root.constant(0, t, 0, base_y+i*30) for i, (n, t) in enumerate(outputs)}
    for col, child in enumerate(children):
        x = 65+col*spacing
        outs = root.instance(child, type_id, x, 15)
        for i, (n, t) in enumerate(outputs):
            y = base_y+i*30
            signals[n] = (root.select(outs["Valid"], signals[n], outs[n], x+spacing-65, y) if t in (3, 4)
                          else root.logic("OR", signals[n], outs[n], x+spacing-65, y))
    for i, (n, t) in enumerate(outputs): root.output(n, t, signals[n], 75+len(children)*spacing, base_y+i*30)
    root.finish()
    return root, {d.uid: d.store for d in [root, *children]}


def write_archive(path, root, documents):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name+".tmp")
    try:
        with zipfile.ZipFile(temp, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("Template", json.dumps(documents, ensure_ascii=False, indent=2))
            metadata = json.loads(json.dumps(root.store["MetaData"]))
            metadata["ComponentMetaData"]["Image"] = library_icon()
            z.writestr("MetaData", json.dumps(metadata, ensure_ascii=False))
            z.writestr("MinRequiredVersion", "2.0")
        temp.replace(path)
    finally:
        if temp.exists(): temp.unlink()


def library_icon():
    """Нейтральная пиктограмма блока с одним входом и девятью выходами."""
    pixels = bytearray([255, 255, 255, 255] * 128 * 128)
    def fill(x0, y0, x1, y1, color):
        for y in range(y0, y1):
            for x in range(x0, x1): pixels[(y*128+x)*4:(y*128+x+1)*4] = bytes(color)
    ink = (36, 65, 95, 255)
    fill(27, 10, 101, 118, ink); fill(30, 13, 98, 115, (237, 244, 250, 255))
    fill(8, 62, 28, 65, ink)
    for i in range(9):
        fill(100, 19+i*11, 120, 22+i*11, ink)
    for y in (32, 52, 72, 92): fill(43, y, 84, y+2, ink)
    raw = b''.join(b'\x00' + pixels[y*512:(y+1)*512] for y in range(128))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 128, 128, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    return base64.b64encode(png).decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("excel", type=Path)
    parser.add_argument("--output", type=Path, default=Path("output/TableParameters.tple"))
    parser.add_argument("--name", default="TableParameters", help="Имя и пространство идентификаторов макроса")
    parser.add_argument("--sheet", help="Обрабатывать только указанный лист")
    args = parser.parse_args()
    try:
        if args.output.suffix.lower() != ".tple": raise ValueError("Выходной файл должен иметь расширение .tple")
        if not args.name.strip(): raise ValueError("Имя макроса не должно быть пустым")
        from fbd_service import generate
        result = generate(args.excel, args.output, sheet=args.sheet, name=args.name)
        print(f"Создан {args.output}: записей {result['records']}, групп {result['groups']}.")
    except (ValueError, KeyError, OSError, zipfile.BadZipFile, ET.ParseError) as exc:
        parser.exit(1, f"Ошибка: {exc}\n")


if __name__ == "__main__": main()
