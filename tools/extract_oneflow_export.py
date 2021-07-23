# python3 -m pip install isort autoflake astpretty
# requires python3.8 to run
import os
import argparse
import ast
from posixpath import basename, relpath
import subprocess
import multiprocessing
from pathlib import Path, PurePosixPath
from functools import partial
from collections import OrderedDict
import astpretty
import sys

parser = argparse.ArgumentParser()
parser.add_argument(
    "--out_dir", type=str, default="python",
)
parser.add_argument("--verbose", "-v", action="store_true")
parser.add_argument("--debug", "-d", action="store_true")
parser.add_argument("--skip_autoflake", "-sa", action="store_true")
parser.add_argument("--skip_black", "-sb", action="store_true")
parser.add_argument("--skip_isort", "-si", action="store_true")
parser.add_argument("--save_ast", "--ast", action="store_true")
args = parser.parse_args()

OUT_PATH = Path(args.out_dir)
SAVE_AST = args.save_ast


def dumpprint(node):
    astpretty.pprint(node)


def is_export_decorator(d):
    return (
        isinstance(d, ast.Call)
        and isinstance(d.func, ast.Name)
        and d.func.id == "oneflow_export"
    )


def get_parent_module(value):
    return ".".join(value.split(".")[0:-1])


def join_module(parent, child):
    if child:
        return ".".join([parent, child])
    else:
        return parent


def path_from_module(module, is_init=False):
    if is_init:
        return Path("/".join(module.split(".") + ["__init__.py"]))
    else:
        return Path("/".join(module.split(".")) + ".py")


def module_from_path(path: Path):
    assert path.name.endswith(".py")
    parts = path.parts
    if parts[-1] == "__init__.py":
        return ".".join(path.parts[0:-1])
    else:
        return ".".join(path.parts)[0:-3]


class ExportVisitor(ast.NodeTransformer):
    def __init__(self, root_module="oneflow") -> None:
        super().__init__()
        self.staging_decorators = []
        self.root_module = root_module
        self.export_modules = {}

    def append_export(self, target_module=None, node=None):
        if target_module not in self.export_modules:
            module = ast.Module(body=[], type_ignores=[])
            self.export_modules[target_module] = module
        else:
            module = self.export_modules[target_module]
        # dumpprint(module)
        module.body.append(node)

    def visit_Expr(self, node):
        if isinstance(node.value, ast.Constant):
            if "Copyright 2020 The OneFlow Authors" in node.value.value:
                return None
        return node

    def visit_ImportFrom(self, node):
        for name in node.names:
            if not self.visit(name):
                return None
        if node.module:
            if node.module == "__future__" or node.module.startswith(
                "oneflow.python.oneflow_export"
            ):
                return None
            if node.module.startswith("oneflow.python."):
                node.module = node.module.replace("oneflow.python.", "oneflow.")
                return node
        return node

    def visit_Import(self, node):
        for name in node.names:
            if not super().visit(name):
                return None
        return node

    def visit_alias(self, node: ast.alias) -> ast.alias:
        if node.name.startswith("oneflow.python."):
            node.name = node.name.replace("oneflow.python.", "oneflow.")
            return node
        elif node.name == "oneflow.python":
            node.name = "oneflow"
        elif node.name == "oneflow_export":
            return None
        elif "__export_symbols__" in node.name:
            return None
        else:
            return node

    def visit_Name(self, node: ast.AST):
        if node.id == "oneflow_export":
            return None
        return node

    def visit_Call(self, node: ast.AST):
        if not self.visit(node.func):
            return None
        return node

    def visit_ClassDef(self, node):
        return self.visit_FunctionDef(node)


    def visit_FunctionDef(self, node):
        compact_decorator_list = [self.visit(d) for d in node.decorator_list]
        compact_decorator_list = [d for d in compact_decorator_list if d]
        for d in node.decorator_list:
            # if @register_tensor_op, export it in __init__.py
            if is_export_decorator(d):
                import_from_exports = []
                target_module = None
                target_name = None
                for (i, arg) in enumerate(d.args):
                    if i == 0:
                        target_module = join_module(
                            self.root_module, get_parent_module(arg.value)
                        )
                        target_name = arg.value.split(".")[-1]
                    asname = None
                    if node.name != target_name:
                        asname = node.name
                    import_from_export = ast.ImportFrom(
                        module=target_module,
                        names=[ast.alias(name=target_name, asname=asname),],
                        level=0,
                    )
                    import_from_exports.append(import_from_export)
                # TODO: insert "from origin_module import *" in exported func body
                # TODO: rename function to target_name
                node.decorator_list = compact_decorator_list
                self.append_export(target_module=target_module, node=node)
                return import_from_exports
        return node


class SrcFile:
    def __init__(self, spec) -> None:
        is_test = "is_test" in spec and spec["is_test"]
        self.export_visitor = None
        self.tree = None
        self.dst = Path(spec["dst"])
        self.src: Path = spec["src"]
        if is_test and args.verbose:
            print("[skip test]", self.src)
        else:
            txt = self.src.read_text()
            self.tree = ast.parse(txt)
            root_module = "oneflow"
            if (
                "compatible_single_client_python" in self.src.parts
                or self.src.name == "single_client_init.py"
                or self.src.name == "single_client_main.py"
            ):
                root_module = "oneflow.compatible.single_client"
            self.export_visitor = ExportVisitor(root_module=root_module)
            self.export_visitor.visit(self.tree)


def get_specs_under_python(python_path=None, dst_path=None):
    specs = []
    for p in Path(python_path).rglob("*.py"):
        if p.name == "version.py":
            continue
        rel = p.relative_to(python_path)
        dst = Path(dst_path).joinpath(rel)
        spec = {"src": p, "dst": dst}
        if rel.parts[0] == "test":
            spec["is_test"] = True
        specs.append(spec)
    return specs


def get_files():
    srcs = (
        get_specs_under_python(python_path="oneflow/python", dst_path="oneflow")
        + get_specs_under_python(
            python_path="oneflow/compatible_single_client_python",
            dst_path="oneflow/compatible/single_client",
        )
        + [
            {"src": Path("oneflow/init.py"), "dst": "oneflow/__init__.py"},
            {"src": Path("oneflow/__main__.py"), "dst": "oneflow/__main__.py"},
            {
                "src": Path("oneflow/single_client_init.py"),
                "dst": "oneflow/compatible/single_client/__init__.py",
            },
            {
                "src": Path("oneflow/single_client_main.py"),
                "dst": "oneflow/compatible/single_client/__main__.py",
            },
        ]
    )
    srcs = list(filter(lambda x: ("oneflow_export" not in x["src"].name), srcs))
    if args.debug:
        srcs = [
            {
                "src": Path("oneflow/python/ops/nn_ops.py"),
                "dst": "oneflow/ops/nn_ops.py",
            },
            {
                "src": Path("oneflow/python/advanced/distribute_ops.py"),
                "dst": "oneflow/advanced/distribute_ops.py",
            },
        ]
    pool = multiprocessing.Pool()
    srcs = pool.map(SrcFile, srcs,)
    pool.close()
    return srcs


class ModuleNode:
    def __init__(self, name=None, parent=None) -> None:
        self.children = dict()
        self.parent = parent
        self.level = 0
        if parent:
            self.level = parent.level + 1
        self.name = name

    def add_or_get_child(self, name):
        if name in self.children:
            return self.children[name]
        else:
            self.children[name] = ModuleNode(name=name, parent=self)
            return self.children[name]

    @property
    def is_leaf(self):
        return len(self.children.keys()) == 0

    def walk(self, cb):
        cb(self)
        for child in self.children.values():
            child.walk(cb)

    @property
    def leafs(self):
        ret = []

        def add_leafs(node: ModuleNode):
            if node.is_leaf:
                ret.append(node)

        self.walk(add_leafs)
        return ret

    @property
    def full_name(self):
        current_parent = self
        ret = self.name
        while current_parent.parent:
            current_parent = current_parent.parent
            ret = current_parent.name + "." + ret
        return ret

    def __str__(self) -> str:
        return "\n".join(
            [f"{self.full_name}"]
            + [child.__str__() for child in self.children.values()]
        )

    @staticmethod
    def add_sub_module(root=None, module=None):
        parts = module.split(".")
        current_node = root
        assert current_node.name == parts[0]
        for part in parts[1::]:
            current_node = current_node.add_or_get_child(part)


def save_trees(args=None):
    dst: Path = args["dst"]
    trees = args["trees"]
    # if len(trees) > 2:
    # print(dst, len(trees))
    dst_full = OUT_PATH.joinpath(dst)
    dst_full.parent.mkdir(parents=True, exist_ok=True)
    dst_full.touch(exist_ok=False)
    # TODO: append "doctest.testmod(raise_on_error=True)"
    if SAVE_AST:
        new_txt = "\n".join(
            [str(astpretty.pformat(ast.fix_missing_locations(tree))) for tree in trees]
        )
        dst_full.with_suffix(".ast.py").write_text(new_txt)
    new_txt = "\n".join([ast.unparse(tree) for tree in trees])
    dst_full.write_text(new_txt)


def append_trees(tree_dict: dict, module: str, tree: ast.AST):
    tree_dict[module] = tree_dict.get(module, [])
    tree_dict[module].append(tree)


if __name__ == "__main__":
    out_oneflow_dir = os.path.join(args.out_dir, "*")
    assert args.out_dir
    assert args.out_dir != "~"
    assert args.out_dir != "/"
    subprocess.check_call(f"mkdir -p {OUT_PATH}", shell=True)
    subprocess.check_call(f"rm -rf {out_oneflow_dir}", shell=True)
    # step 0: parse and load all segs into memory
    srcs = get_files()
    final_trees = {}

    root_module = ModuleNode(name="oneflow")
    src_module_added = {}
    for s in srcs:
        # src
        target_module = module_from_path(s.dst)
        append_trees(tree_dict=final_trees, module=target_module, tree=s.tree)
        if (
            str(s.src) == "oneflow/python/__init__.py"
            or str(s.src) == "oneflow/compatible_single_client_python/__init__.py"
        ):
            assert not s.src.read_text()
            continue
        print("[src]", target_module, s.src)
        assert target_module not in src_module_added, {
            "target_module": target_module,
            "new": str(s.src),
            "exist": str(src_module_added[target_module]),
        }
        src_module_added[target_module] = s.src
        ModuleNode.add_sub_module(root=root_module, module=target_module)
    for s in srcs:
        # exports
        for export_path, export_tree in s.export_visitor.export_modules.items():
            print("[export]", export_path)
            append_trees(tree_dict=final_trees, module=export_path, tree=export_tree)
            ModuleNode.add_sub_module(root=root_module, module=export_path)
    # print(root_module)
    leaf_modules = set([leaf.full_name for leaf in root_module.leafs])
    pool = multiprocessing.Pool()

    print("leaf_modules", leaf_modules)

    def is_init(module: str):
        is_leaf = module in leaf_modules
        is_magic = module.endswith("__")
        if is_magic:
            print("[magic]", module)
        if is_leaf == False and is_magic == False:
            print("[is_init]", module)
        return is_leaf == False and is_magic == False

    srcs = pool.map(
        save_trees,
        [
            {"dst": path_from_module(module, is_init=is_init(module)), "trees": trees,}
            for module, trees in final_trees.items()
        ],
    )
    pool.close()
    # step 1: extract all exports
    # step 2: merge files under python/ into generated files
    # step 3: rename all
    # step 4: finalize __all__, if it is imported by another module or wrapped in 'oneflow.export', it should appears in __all__
    # step 5: save file and sort imports and format
    extra_arg = ""
    if args.verbose == False:
        extra_arg += "--quiet"
    if args.skip_autoflake == False:
        print("[postprocess]", "autoflake")
        subprocess.check_call(
            f"{sys.executable} -m autoflake --in-place --remove-all-unused-imports --recursive .",
            shell=True,
            cwd=args.out_dir,
        )
    if args.skip_isort == False:
        print("[postprocess]", "isort")
        subprocess.check_call(
            f"{sys.executable} -m isort . {extra_arg}", shell=True, cwd=args.out_dir,
        )
    if args.skip_black == False:
        print("[postprocess]", "black")
        subprocess.check_call(
            f"{sys.executable} -m black . {extra_arg}", shell=True, cwd=args.out_dir,
        )