#!/usr/bin/env python3
import ast
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN_DIRS = ("capa_fisica", "tools", "tests")
FORBIDDEN_MODULES = ("dataclasses", "secrets", "contextvars")
FORBIDDEN_NODE_NAMES = (
    "JoinedStr",
    "FormattedValue",
    "AnnAssign",
    "NamedExpr",
    "Match",
    "MatchCase",
    "TryStar",
    "TypeAlias",
    "ParamSpec",
    "TypeVarTuple",
)


def source_files():
    for directory in SCAN_DIRS:
        base = os.path.join(ROOT, directory)
        for current, dirs, files in os.walk(base):
            dirs[:] = [item for item in dirs if item != "__pycache__"]
            for name in sorted(files):
                if name.endswith(".py"):
                    yield os.path.join(current, name)


def parse_with_feature_version(source, path):
    try:
        return ast.parse(source, filename=path, feature_version=(3, 5)), None
    except TypeError:
        return ast.parse(source, filename=path), None
    except SyntaxError as exc:
        return None, exc


def forbidden_nodes(tree):
    names = set()
    for node in ast.walk(tree):
        for name in FORBIDDEN_NODE_NAMES:
            node_type = getattr(ast, name, None)
            if node_type is not None and isinstance(node, node_type):
                names.add(name)
    return sorted(names)


def forbidden_imports(tree):
    found = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                top = alias.name.split(".")[0]
                if top in FORBIDDEN_MODULES:
                    found.append(alias.name)
        elif isinstance(node, ast.ImportFrom):
            module = node.module or ""
            top = module.split(".")[0]
            if top in FORBIDDEN_MODULES:
                found.append(module)
    return sorted(set(found))


def check_file(path):
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()
    tree, error = parse_with_feature_version(source, path)
    if error is not None:
        return ["syntax incompatible with Python 3.5: %s" % error]
    problems = []
    nodes = forbidden_nodes(tree)
    if nodes:
        problems.append("forbidden syntax: %s" % ", ".join(nodes))
    imports = forbidden_imports(tree)
    if imports:
        problems.append("forbidden modules: %s" % ", ".join(imports))
    return problems


def main():
    checked = 0
    failures = 0
    for path in source_files():
        checked += 1
        problems = check_file(path)
        if problems:
            failures += 1
            relative = os.path.relpath(path, ROOT)
            sys.stdout.write("%s\n" % relative)
            for problem in problems:
                sys.stdout.write("  - %s\n" % problem)
    if failures:
        sys.stdout.write(
            "FAILED: %d of %d files have compatibility problems\n"
            % (failures, checked)
        )
        return 1
    sys.stdout.write("OK: %d files are compatible with Python 3.5 grammar\n" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
