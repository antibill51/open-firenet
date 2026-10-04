#!/usr/bin/env python3
"""Keeps openapi.yaml in line with the firmware. The description is written by hand (the firmware builds its JSON
with format strings, there is nothing to generate it from), so this test is what makes a change of the API without
its description fail in CI:

1. The description is well formed: known keywords only, references that exist, no YAML trap (`off` read as false...).
2. Against the source code: every route registered by the firmware is described and the other way round; the fields of
   the "device", "stove", "sensors" and "controls" objects are the ones the firmware writes; every command field is a
   name the firmware accepts; the MQTT statuses are the ones the firmware returns.
3. Against real answers of a bridge (tools/screenshot_fixtures/, a DOMO 2.29): types, null allowed or not,
   enumerations, and no field returned without being described.

Needs PyYAML only.
"""
import json, pathlib, sys
import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
SPEC = yaml.safe_load((ROOT / "openapi.yaml").read_text())
FIX = ROOT / "tools" / "screenshot_fixtures"
errors = []


def resolve(schema):
    while "$ref" in schema:
        node = SPEC
        for part in schema["$ref"].lstrip("#/").split("/"):
            node = node[part]
        schema = node
    return schema


TYPES = {"string": str, "boolean": bool, "object": dict, "array": list}


def check(value, schema, path, strict):
    """strict: a key missing from "properties" is an error unless the schema allows additional properties."""
    schema = resolve(schema)
    if value is None:
        if not schema.get("nullable"):
            errors.append(f"{path}: null is not allowed")
        return
    t = schema.get("type")
    if t == "integer":
        good = isinstance(value, int) and not isinstance(value, bool)
    elif t == "number":
        good = isinstance(value, (int, float)) and not isinstance(value, bool)
    else:
        good = t is None or isinstance(value, TYPES[t])
    if not good:
        errors.append(f"{path}: {value!r} is not of type {t}")
        return
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: {value!r} is not one of {schema['enum']}")
    if "minimum" in schema and value < schema["minimum"] or "maximum" in schema and value > schema["maximum"]:
        errors.append(f"{path}: {value!r} is out of range")
    if t == "object":
        props, extra = schema.get("properties", {}), schema.get("additionalProperties")
        for key in schema.get("required", []):
            if key not in value:
                errors.append(f"{path}: required field {key} is missing")
        for key, v in value.items():
            if key in props:
                check(v, props[key], f"{path}.{key}", strict)
            elif isinstance(extra, dict):
                check(v, extra, f"{path}.{key}", strict)
            elif strict and extra is not True:
                errors.append(f"{path}.{key}: returned by the firmware but not described")
    if t == "array":
        for i, v in enumerate(value):
            check(v, schema["items"], f"{path}[{i}]", strict)


def refs(node, path="#"):
    if isinstance(node, dict):
        if "$ref" in node:
            try:
                resolve(node)
            except (KeyError, TypeError):
                errors.append(f"{path}: {node['$ref']} does not exist")
        for k, v in node.items():
            refs(v, f"{path}/{k}")
    elif isinstance(node, list):
        for i, v in enumerate(node):
            refs(v, f"{path}/{i}")


def answer(path, method="get"):
    return SPEC["paths"][path][method]["responses"]["200"]["content"]["application/json"]["schema"]


KEYWORDS = {"type", "properties", "required", "items", "enum", "description", "nullable", "minimum", "maximum",
            "additionalProperties", "default", "example", "$ref"}


def wellformed(node, path):
    """Schemas use known keywords only: a description holding a comma inside a `{ ... }` mapping, for example, is
    split by YAML into extra keys; an unquoted `off` or `on` becomes a boolean."""
    if not isinstance(node, dict):
        return
    for k in node:
        if k not in KEYWORDS:
            errors.append(f"{path}: unknown schema keyword {k!r} (a YAML quoting problem?)")
    for v in node.get("enum", []):
        if isinstance(v, bool) or v is None:
            errors.append(f"{path}: enum value {v!r} (quote it)")
    for k, v in node.get("properties", {}).items():
        if not isinstance(k, str):
            errors.append(f"{path}: property name {k!r} is not a string (quote it)")
        wellformed(v, f"{path}.{k}")
    for k in ("items", "additionalProperties"):
        if isinstance(node.get(k), dict):
            wellformed(node[k], f"{path}.{k}")


for name, schema in SPEC["components"]["schemas"].items():
    wellformed(schema, name)

refs(SPEC)
for fixture, schema in (("state.json", answer("/api/state")), ("controls.json", answer("/api/controls")),
                        ("schedule.json", answer("/api/schedule")), ("mqtt.json", answer("/api/mqtt")),
                        ("txgap.json", answer("/api/txgap"))):
    check(json.loads((FIX / fixture).read_text()), schema, fixture, strict=True)

# The documented examples of the requests are valid commands.
for path, item in SPEC["paths"].items():
    for method, op in item.items():
        body = op.get("requestBody", {}).get("content", {}).get("application/json")
        if not body:
            continue
        examples = [e["value"] for e in body.get("examples", {}).values()] + ([body["example"]] if "example" in body else [])
        for i, ex in enumerate(examples):
            check(ex, body["schema"], f"{method.upper()} {path} example {i + 1}", strict=True)

# ---- Against the source code
import re
ino = (ROOT / "open-firenet" / "open-firenet.ino").read_text()
api_h = (ROOT / "open-firenet" / "firenet_api.h").read_text()
schemas = SPEC["components"]["schemas"]


def both_ways(what, in_code, in_spec):
    for x in sorted(set(in_code) - set(in_spec)):
        errors.append(f"{what}: {x} is in the firmware but not described")
    for x in sorted(set(in_spec) - set(in_code)):
        errors.append(f"{what}: {x} is described but not in the firmware")


# Routes (captive portal probes and the page itself aside; /api/control is an alias of /api/controls).
routes = set(re.findall(r'web\.on\("(/(?:api/[a-z]+|log))"', ino)) - {"/api/control"}
both_ways("route", routes, SPEC["paths"])


def written_fields(source):
    """{object name: field names} of the JSON objects written by a block of format strings."""
    out, current = {}, None
    for name, opens in re.findall(r'\\"(\w+)\\":(\{)?', source):
        if opens:
            current = out.setdefault(name, [])
        elif current is not None:
            current.append(name)
    return out


sections = ino[ino.index("static String jsonStoveSections()"):ino.index("static String jsonState()")]
state_fn = ino[ino.index("static String jsonState()"):]
device = state_fn[:state_fn.index("jsonStoveSections()")]
fields = {**written_fields(sections), **written_fields(device)}
for obj, schema in (("device", "Device"), ("stove", "Stove"), ("sensors", "Sensors"), ("controls", "ControlsState")):
    if obj not in fields:
        errors.append(f"{obj}: object not found in the firmware source (was the code reorganised?)")
        continue
    both_ways(f"{obj} field", fields[obj], schemas[schema]["properties"])

# Commands: every described field is a name the firmware accepts (CONTROL_PARAMS of firenet_api.h).
accepted = set(" ".join(re.findall(r'ParamKind::\w+,\s*"([^"]+)"\}', api_h)).split())
if not accepted:
    errors.append("CONTROL_PARAMS not found in firenet_api.h (was the code reorganised?)")
for name in schemas["ControlsCommand"]["properties"]:
    if name not in accepted:
        errors.append(f"command field {name}: not accepted by the firmware")

# The description carries the version of the firmware it describes (scripts/release.sh updates both).
fw_version = re.search(r'#define OPENFIRENET_VERSION "([^"]+)"', ino).group(1)
if str(SPEC["info"]["version"]) != fw_version:
    errors.append(f"info.version is {SPEC['info']['version']} but the firmware is {fw_version}")

# MQTT statuses: the texts returned by mqttStatus() and by the answer to a POST.
mqtt_fn = ino[ino.index("static const char* mqttStatus()"):]
mqtt_fn = mqtt_fn[:mqtt_fn.index("\n}\n")]
both_ways("MQTT status", set(re.findall(r'return "([^"]+)"', mqtt_fn)), schemas["Mqtt"]["properties"]["status"]["enum"])

for e in errors:
    print("ECHEC", e)
print(f"openapi: {len(SPEC['paths'])} paths, {len(SPEC['components']['schemas'])} schemas, {len(errors)} failures")
sys.exit(1 if errors else 0)
