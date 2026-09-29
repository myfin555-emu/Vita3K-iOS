"""Generate/validate real SPIR-V and convert with MoltenVK's pinned SPIRV-Cross.

The small interpreter checks the fixture's control flow and storage writes; it
is not a GPU, derivative or Metal-driver correctness test.
"""
import pathlib
import shlex
import subprocess
import sys
import tempfile

fixture, validator, disassembler, cross = sys.argv[1:]


def run(*args):
    return subprocess.check_output(args, text=True)


def interpret(text, kill_phase):
    values, functions, names = {}, {}, {}
    current = None
    entry = None
    for line in text.splitlines():
        tokens = shlex.split(line)
        if not tokens or tokens[0].startswith(';'):
            continue
        result = None
        if len(tokens) > 1 and tokens[1] == '=':
            result, tokens = tokens[0], tokens[2:]
        op, args = tokens[0], tokens[1:]
        if op == 'OpEntryPoint':
            entry = args[1]
        elif op == 'OpName':
            names[args[1]] = args[0]
        elif op == 'OpConstant':
            values[result] = float(args[1])
        elif op == 'OpConstantFalse':
            values[result] = False
        elif op == 'OpConstantTrue':
            values[result] = True
        elif op == 'OpVariable':
            values[result] = values[args[2]] if len(args) > 2 else 0
        elif op == 'OpFunction':
            current = []
            functions[result] = current
        elif op == 'OpFunctionEnd':
            current = None
        elif current is not None:
            current.append((result, op, args))
    values[names['kill_phase']] = kill_phase
    writes, begins, ends = 0, 0, 0

    class Killed(Exception):
        pass

    def call(fn):
        nonlocal writes, begins, ends
        code = functions[fn]
        labels = {r: i for i, (r, op, a) in enumerate(code) if op == 'OpLabel'}
        pc = 0
        while pc < len(code):
            result, op, args = code[pc]
            pc += 1
            if op in ('OpLabel', 'OpSelectionMerge'):
                continue
            if op == 'OpLoad':
                values[result] = values[args[1]]
            elif op == 'OpStore':
                values[args[0]] = values[args[1]]
            elif op == 'OpLogicalNot':
                values[result] = not values[args[1]]
            elif op == 'OpFOrdEqual':
                values[result] = values[args[1]] == values[args[2]]
            elif op == 'OpBranchConditional':
                pc = labels[args[1] if values[args[0]] else args[2]]
            elif op == 'OpBranch':
                pc = labels[args[0]]
            elif op == 'OpFunctionCall':
                call(args[1])
            elif op == 'OpReturn':
                return
            elif op == 'OpKill':
                raise Killed()
            elif op == 'OpImageWrite':
                writes += 1
            elif op == 'OpBeginInvocationInterlockEXT':
                begins += 1
            elif op == 'OpEndInvocationInterlockEXT':
                ends += 1
            else:
                raise AssertionError('Unhandled fixture instruction: ' + op)
    try:
        call(entry)
    except Killed:
        pass
    return writes, begins, ends


with tempfile.TemporaryDirectory() as folder:
    folder = pathlib.Path(folder)
    for mode in ('native', 'deferred'):
        path = folder / (mode + '.spv')
        run(fixture, mode, str(path))
        run(validator, '--target-env', 'vulkan1.1', str(path))
        text = run(disassembler, '--raw-id', '--no-header', str(path))
        for kill_phase, expected_writes in ((0, 2), (1, 0), (2, 0)):
            writes, begins, ends = interpret(text, kill_phase)
            assert writes == expected_writes, (mode, kill_phase, writes)
            assert begins == 1
            if mode == 'deferred':
                assert ends == 1, 'interlock must be released even after discard'
        if mode == 'deferred':
            assert 'OpKill' not in text
        else:
            assert 'OpKill' in text, 'non-interlock/native path must retain discard'
        for arguments in (False, True):
            options = ['--msl', '--msl-ios', '--msl-version', '20300',
                       '--msl-check-discarded-frag-stores']
            if arguments:
                options.append('--msl-argument-buffers')
            msl = run(cross, str(path), *options)
            if mode == 'deferred':
                assert 'simd_is_helper_thread' not in msl
                assert 'discard_fragment' not in msl
                assert 'fragment_discarded' in msl and '.write(' in msl
            else:
                # Reproduce the source pattern named in the actual device error.
                assert 'simd_is_helper_thread' in msl
                assert 'discard_fragment' in msl
print('SPIR-V validation, discard/output semantics and four MSL conversions passed')
