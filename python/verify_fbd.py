"""Независимый расчёт выходов по соединениям сформированной схемы FBD."""
from excel_to_fbd import model_of


def require(condition, message="Нарушена структура схемы"):
    if not condition:
        raise ValueError(message)


def validate_structure(documents):
    all_ids = set()
    for uid, item in documents.items():
        doc = item["DocumentModel"]
        require(uid == doc["UniqueId"] == item["MetaData"]["ComponentMetaData"]["UniqueId"])
        ports, driven = {}, set()
        for e in doc["Elements"]:
            m = model_of(e)
            require(m["UniqueId"] not in all_ids)
            all_ids.add(m["UniqueId"])
            anchors = {p.get("UniqueId") for p in m["Primitives"]}
            for p in m["Ports"]:
                require(p["UniqueId"] not in all_ids)
                all_ids.add(p["UniqueId"])
                require(p["AnchorUniqueId"] in anchors)
                ports[p["UniqueId"]] = p
            if e["Discriminator"] == 37:
                child = documents[e["MacroUniqueId"]]
                require(e["LastUpdatingId"] == child["LastUpdatingId"])
                child_elements = {model_of(v)["UniqueId"]: v for v in child["DocumentModel"]["Elements"]}
                for p in m["Ports"]:
                    target = child_elements[p["ElementOwnerUId"]]
                    require(target["Discriminator"] == (38 if p["PortType"] == 0 else 39))
                    require(model_of(target)["Ports"][0]["DataType"] == p["DataType"])
        for c in doc["ConnectorModels"]:
            w = c["ConnectorModel"]
            a, b = ports[w["FromPortUId"]], ports[w["ToPortUId"]]
            require(a["PortType"] == 1 and b["PortType"] == 0)
            require(a["DataType"] == b["DataType"])
            require(b["UniqueId"] not in driven)
            driven.add(b["UniqueId"])
        require(driven == {u for u, p in ports.items() if p["PortType"] == 0})


class CompiledDiagram:
    """Порядок вычисления из связей; длинные схемы не используют стек Python."""
    def __init__(self, documents, uid, stack=()):
        from collections import deque
        require(uid not in stack, "Циклическая вложенность макросов")
        doc = documents[uid]["DocumentModel"]
        elements = {model_of(e)["UniqueId"]: e for e in doc["Elements"]}
        self.incoming = {c["ConnectorModel"]["ToPortUId"]: c["ConnectorModel"]["FromPortUId"]
                         for c in doc["ConnectorModels"]}
        owners = {p["UniqueId"]: k for k, e in elements.items() for p in model_of(e)["Ports"]}
        dependencies, followers = {}, {k: set() for k in elements}
        for k, e in elements.items():
            dependencies[k] = {owners[self.incoming[p["UniqueId"]]]
                               for p in model_of(e)["Ports"] if p["PortType"] == 0}
            for dep in dependencies[k]: followers[dep].add(k)
        queue = deque(k for k in elements if not dependencies[k])
        self.ordered = []
        while queue:
            key = queue.popleft()
            e = elements[key]; m = model_of(e)
            sources = [self.incoming[p["UniqueId"]] for p in m["Ports"] if p["PortType"] == 0]
            outputs = [p for p in m["Ports"] if p["PortType"] == 1]
            child, child_names = None, []
            if e["Discriminator"] == 37:
                child = CompiledDiagram(documents, e["MacroUniqueId"], (*stack, uid))
                child_elements = {model_of(v)["UniqueId"]: v for v in documents[e["MacroUniqueId"]]["DocumentModel"]["Elements"]}
                child_names = [child_elements[p["ElementOwnerUId"]]["OutputName"] for p in outputs]
            self.ordered.append((e, sources, outputs, child, child_names))
            for follower in followers[key]:
                dependencies[follower].remove(key)
                if not dependencies[follower]: queue.append(follower)
        require(len(self.ordered) == len(elements), "Циклическая связь в схеме")

    def evaluate(self, key):
        values, result = {}, {}
        for e, sources, outputs, child, child_names in self.ordered:
            args = [values[p] for p in sources]
            kind, fb = e["Discriminator"], model_of(e)["FbType"]
            if kind == 38: output = [key]
            elif kind == 39:
                result[e["OutputName"]] = args[0]; continue
            elif kind == 29: output = [e["Value"]]
            elif kind == 21 and fb == 25: output = [args[0] == args[1]]
            elif kind in (21, 22) and fb == 20: output = [args[2] if args[0] else args[1]]
            elif kind == 18 and fb == 4: output = [bool(args[0] or args[1])]
            elif kind == 18 and fb == 5: output = [bool(args[0] and args[1])]
            elif kind == 37:
                sub = child.evaluate(args[0]); output = [sub[n] for n in child_names]
            else: raise ValueError(f"Неизвестный блок {kind}/{fb}")
            for port, value in zip(outputs, output): values[port["UniqueId"]] = value
        return result


def evaluate(documents, uid, key):
    return CompiledDiagram(documents, uid).evaluate(key)


def verify(documents, root_uid, rows):
    validate_structure(documents)
    compiled = CompiledDiagram(documents, root_uid)
    outputs = documents[root_uid]["MetaData"]["MetaDataOutputs"]
    by_key = {r["key"]: r["values"] for r in rows}
    cases = {0, 1, 9, 10, 19, 20, 49, 50, 4294967295}
    for k in by_key:
        cases.update(range((k//10)*10, min((k//10)*10+10, 4294967296)))
        cases.update(v for v in (k-1, k, k+1) if 0 <= v <= 4294967295)
    for key in sorted(cases):
        got = compiled.evaluate(key)
        expected = {p["Name"]: 0 for p in outputs}
        if key in by_key: expected.update(by_key[key]); expected["Valid"] = True
        require(got == expected, f"№ типа {key}: {got} вместо {expected}")
    return len(cases)
