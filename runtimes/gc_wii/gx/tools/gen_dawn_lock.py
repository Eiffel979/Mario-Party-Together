#!/usr/bin/env python3
"""Generate runtimes/gc_wii/gx/dawn.lock from a Dawn checkout (developer tool, run on a pin bump).

ADR 0009, amendment after P1-14 gate, section 2: Dawn's DEPS is pinned through lock_version 2
`deps` and `skipped_deps`. Every DEPS entry is either pinned (a full lock entry with repo, commit,
tree and license) or skipped with a reason. This tool writes that list; asset-import's fetcher
cross-checks it against DEPS on every fetch.

DEPS is read with a small literal evaluator (strings, dicts, lists, True/False/None, '+',
Var() and Str()); nothing in it is executed. The commit tree of each pinned dependency is found
by a shallow fetch of that one commit into a scratch directory, which is deleted afterwards.

Usage:
  python3 -I gen_dawn_lock.py --dawn <checkout at the pin> --out dawn.lock [--scratch DIR]
"""
import argparse
import ast
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DAWN_REPO = 'https://dawn.googlesource.com/dawn'
DAWN_LICENSE = 'BSD-3-Clause'

# The dependencies our CMake build of webgpu_dawn uses (shared library, D3D11/D3D12/Vulkan/Null
# backends, no GL, no tests, no samples, no protobuf, no GLFW, FXC instead of DXC).
# path -> (lock source name, SPDX license). Checked against each repository's license file.
NEEDED = {
    'third_party/abseil-cpp': ('dawn_abseil_cpp', 'Apache-2.0'),
    'third_party/jinja2': ('dawn_jinja2', 'BSD-3-Clause'),
    'third_party/markupsafe': ('dawn_markupsafe', 'BSD-3-Clause'),
    'third_party/spirv-headers/src': ('dawn_spirv_headers', 'MIT AND CC-BY-4.0'),
    'third_party/spirv-tools/src': ('dawn_spirv_tools', 'Apache-2.0'),
    'third_party/vulkan-headers/src': ('dawn_vulkan_headers', 'Apache-2.0 AND MIT'),
    'third_party/vulkan-utility-libraries/src': ('dawn_vulkan_utility_libraries', 'Apache-2.0 AND MIT AND CC-BY-4.0'),
    'third_party/webgpu-headers/src': ('dawn_webgpu_headers', 'BSD-3-Clause'),
}

# Reasons for everything else, by first matching pattern.
SKIP_REASONS = [
    (r'^third_party/directx-shader-compiler/src$',
     'DXC is not built: D3D12 compiles shaders with FXC (ADR 0007, P1-14 gate amendment, patch 9).'),
    (r'^third_party/(googletest|google_benchmark/src|libFuzzer/src|webgpu-cts|catapult)$',
     'Tests, benchmarks and fuzzers are not built.'),
    (r'^third_party/(protobuf|libprotobuf-mutator/src)$|^tools/protoc_wrapper$',
     'Protobuf is off (DAWN_BUILD_PROTOBUF=OFF).'),
    (r'^third_party/glfw3/src$', 'GLFW windowing is off (DAWN_USE_GLFW=OFF); the renderer is headless.'),
    (r'^third_party/(OpenGL-Registry/src|EGL-Registry/src|angle)$', 'The OpenGL and OpenGL ES backends are off.'),
    (r'^third_party/(glslang/src)$', 'Only needed for Tint command-line tools and the GLSL validator, which are off.'),
    (r'^third_party/(vulkan-loader/src|vulkan-tools/src|vulkan-validation-layers/src|vulkan-deps|'
     r'vulkan_memory_allocator|swiftshader)$',
     'Not used by this build: the Vulkan loader comes from the system, and SwiftShader and validation layers are off.'),
    (r'^third_party/(agility-sdk/src|directx-headers/src)$',
     'Not used: D3D12 headers come from the Windows SDK, and DXC is off.'),
    (r'^third_party/(node-api-headers|node-addon-api|node/.*|gpuweb|emsdk)$',
     'Node bindings and Emscripten are not built.'),
    (r'^third_party/(jsoncpp|langsvr)$', 'The WGSL language server is not built.'),
    (r'^third_party/(mesa/src|meson/src|libdrm/src)$', 'Mesa (TINT_BUILD_MESA) is not built.'),
    (r'^third_party/(zlib|partition_alloc|placeholder_chromium)$', 'Chromium-only dependency, not used by the CMake build.'),
    (r'^third_party/(libc\+\+/src|libc\+\+abi/src|llvm-libc/src|clang-format/script|depot_tools|ninja|siso/cipd)$|'
     r'^(buildtools.*|build.*|tools/.*|testing)$',
     'GN/Chromium toolchain and tooling; we build with CMake and our own compiler.'),
]


class _Var:
    def __init__(self, name):
        self.name = name


def _eval(node, variables):
    """Evaluate the literal subset gclient DEPS files use. Anything else is an error."""
    if isinstance(node, ast.Constant) and isinstance(node.value, (str, bool, type(None), int)):
        return node.value
    if isinstance(node, ast.Dict):
        return {_eval(k, variables): _eval(v, variables) for k, v in zip(node.keys, node.values)}
    if isinstance(node, ast.List):
        return [_eval(e, variables) for e in node.elts]
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        left, right = _eval(node.left, variables), _eval(node.right, variables)
        return _str(left, variables) + _str(right, variables)
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and len(node.args) == 1 and not node.keywords:
        arg = _eval(node.args[0], variables)
        if node.func.id == 'Var':
            return _Var(arg)
        if node.func.id == 'Str':
            return arg
    raise ValueError(f'unsupported DEPS construct at line {getattr(node, "lineno", "?")}: {ast.dump(node)[:80]}')


def _str(value, variables):
    if isinstance(value, _Var):
        return '{' + value.name + '}'
    if isinstance(value, str):
        return value
    raise ValueError(f'cannot concatenate {value!r}')


def read_deps(path):
    tree = ast.parse(Path(path).read_text(encoding='utf-8'), filename=str(path))
    scope = {}
    for stmt in tree.body:
        if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
            scope[stmt.targets[0].id] = _eval(stmt.value, scope)
        elif isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Constant):
            continue  # docstring
        else:
            raise ValueError(f'unsupported DEPS statement at line {stmt.lineno}')
    variables = {k: (v.name if isinstance(v, _Var) else v) for k, v in scope.get('vars', {}).items()}
    out = {}
    for dep_path, entry in scope.get('deps', {}).items():
        url = entry if isinstance(entry, str) else entry.get('url')
        if isinstance(url, _Var):
            url = '{' + url.name + '}'
        if url is None or (isinstance(entry, dict) and entry.get('dep_type') == 'cipd'):
            out[dep_path] = None  # CIPD package
            continue
        for _ in range(4):
            url = url.format(**{k: str(v) for k, v in variables.items()})
        out[dep_path] = url
    return out


def git(*args, cwd=None):
    return subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def tree_of(url, commit, scratch):
    d = Path(tempfile.mkdtemp(dir=scratch))
    try:
        git('init', '-q', str(d))
        git('-c', 'core.longpaths=true', 'fetch', '-q', '--depth', '1', '--filter=blob:none', url, commit, cwd=d)
        return git('rev-parse', 'FETCH_HEAD^{tree}', cwd=d)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dawn', required=True, help='Dawn checkout at the pinned commit (clean)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--scratch', default=None)
    a = ap.parse_args()

    dawn = Path(a.dawn)
    commit = git('rev-parse', 'HEAD', cwd=dawn)
    tree = git('rev-parse', 'HEAD^{tree}', cwd=dawn)
    deps = read_deps(dawn / 'DEPS')

    scratch = Path(a.scratch or tempfile.mkdtemp())
    scratch.mkdir(parents=True, exist_ok=True)
    pinned, skipped, entries = [], [], []
    for dep_path in sorted(deps):
        url = deps[dep_path]
        if dep_path in NEEDED:
            name, license_id = NEEDED[dep_path]
            if url is None or '@' not in url:
                sys.exit(f'{dep_path} is needed but DEPS does not pin it to a git commit')
            repo, dep_commit = url.rsplit('@', 1)
            if not re.fullmatch(r'[0-9a-f]{40}', dep_commit):
                sys.exit(f'{dep_path} is pinned by {dep_commit!r}, not a commit hash')
            print(f'tree of {dep_path} ...', flush=True)
            entries.append({'name': name, 'repo': repo, 'commit': dep_commit,
                            'tree': tree_of(repo, dep_commit, scratch), 'license': license_id})
            pinned.append({'path': dep_path, 'source': name})
            continue
        for pattern, reason in SKIP_REASONS:
            if re.search(pattern, dep_path):
                skipped.append({'path': dep_path, 'reason': reason})
                break
        else:
            sys.exit(f'No pin and no skip reason for DEPS entry {dep_path}; extend NEEDED or SKIP_REASONS.')
    missing = set(NEEDED) - set(deps)
    if missing:
        sys.exit(f'DEPS no longer lists {sorted(missing)}')

    lock = {
        'lock_version': 2,
        'sources': [{'name': 'dawn', 'repo': DAWN_REPO, 'commit': commit, 'tree': tree, 'license': DAWN_LICENSE,
                     'deps': pinned, 'skipped_deps': skipped}] + entries,
    }
    Path(a.out).write_text(json.dumps(lock, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'wrote {a.out}: {len(pinned)} pinned, {len(skipped)} skipped')


if __name__ == '__main__':
    main()
