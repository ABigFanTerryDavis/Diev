#!/usr/bin/env python3
"""EOSCWA - E Operating System Custom Written Assembler (subset).
Copyright (C) 2026 EOS contributors.
SPDX-License-Identifier: GPL-3.0-or-later. See LICENSE for details.

Assembles exactly the x86 subset EOS uses (boot.asm, kernel_entry.asm,
isr.asm), chosen to match NASM byte-for-byte on those files:
  usage: eos_cwa.py -f bin|win32 [-o out] in.asm

Supported: BITS 16/32, ORG, section .text, global/extern, %define,
%ifidn __OUTPUT_FORMAT__ ..., %macro/%endmacro with %1..%9 params,
labels (incl. .local), db/dw/dq/times/align, $/$$, full expressions.
-m32-style operand-size overrides emitted as needed (0x66).

win32 output: single .text section COFF (REL32 relocs for external
refs only, NASM-style symbol table with .file/.text/.absolut/@feat.00).
bin output: flat image from ORG.
"""
import re
import struct
import sys

# ---------------------------------------------------------------- regs ---

REGS8 = {'al': 0, 'cl': 1, 'dl': 2, 'bl': 3,
         'ah': 4, 'ch': 5, 'dh': 6, 'bh': 7}
REGS16 = {'ax': 0, 'cx': 1, 'dx': 2, 'bx': 3,
          'sp': 4, 'bp': 5, 'si': 6, 'di': 7}
REGS32 = {'eax': 0, 'ecx': 1, 'edx': 2, 'ebx': 3,
          'esp': 4, 'ebp': 5, 'esi': 6, 'edi': 7}
SREGS = {'es': 0, 'cs': 1, 'ss': 2, 'ds': 3, 'fs': 4, 'gs': 5}
CREGS = {'cr0': 0}
JCC = {'jb': 0x72, 'jae': 0x73, 'je': 0x74, 'jz': 0x74,
       'jne': 0x75, 'jnz': 0x75}
JCC32 = {'jb': 0x82, 'jae': 0x83, 'je': 0x84, 'jz': 0x84,
         'jne': 0x85, 'jnz': 0x85}


def reginfo(name):
    if name in REGS8:
        return ('r', 8, REGS8[name])
    if name in REGS16:
        return ('r', 16, REGS16[name])
    if name in REGS32:
        return ('r', 32, REGS32[name])
    if name in SREGS:
        return ('s', 16, SREGS[name])
    if name in CREGS:
        return ('c', 32, CREGS[name])
    return None


# ---------------------------------------------------------- expressions ---

class Expr:
    pass


def tokenize_expr(s):
    toks = []
    i = 0
    while i < len(s):
        c = s[i]
        if c.isspace():
            i += 1
        elif c in '+-*/%()':
            toks.append(c)
            i += 1
        elif c == '$':
            if i + 1 < len(s) and s[i + 1] == '$':
                toks.append('$$')
                i += 2
            else:
                toks.append('$')
                i += 1
        elif c.isdigit() or (c == '0' and i + 1 < len(s) and s[i + 1] in 'xX'):
            m = re.match(r'0[xX][0-9a-fA-F]+|\d+', s[i:])
            toks.append(('num', m.group(0)))
            i += len(m.group(0))
        elif c.isalpha() or c in '._':
            m = re.match(r'[A-Za-z0-9_.]+', s[i:])
            toks.append(('sym', m.group(0)))
            i += len(m.group(0))
        else:
            raise ValueError(f'bad char in expression: {c!r} ({s!r})')
    return toks


def numval(text):
    text = text.lower()
    if text.startswith('0x'):
        return int(text, 16)
    return int(text, 10)


class ExprParser:
    def __init__(self, toks, syms, dollar, dollardollar):
        self.toks = toks
        self.pos = 0
        self.syms = syms
        self.dollar = dollar
        self.dollardollar = dollardollar

    def peek(self):
        return self.toks[self.pos] if self.pos < len(self.toks) else None

    def next(self):
        t = self.peek()
        self.pos += 1
        return t

    def parse(self):
        v = self.add()
        if self.pos != len(self.toks):
            raise ValueError(f'trailing tokens: {self.toks[self.pos:]}')
        return v

    def add(self):
        v = self.mul()
        while self.peek() in ('+', '-'):
            op = self.next()
            r = self.mul()
            v = v + r if op == '+' else v - r
        return v

    def mul(self):
        v = self.unary()
        while self.peek() in ('*', '/', '%'):
            op = self.next()
            r = self.unary()
            if op == '*':
                v = v * r
            elif op == '/':
                v = v // r if r != 0 else 0
            else:
                v = v % r if r != 0 else 0
        return v

    def unary(self):
        if self.peek() == '-':
            self.next()
            return -self.unary()
        if self.peek() == '+':
            self.next()
            return self.unary()
        return self.atom()

    def atom(self):
        t = self.next()
        if t == '(':
            v = self.add()
            assert self.next() == ')', 'missing )'
            return v
        if t == '$':
            return self.dollar
        if t == '$$':
            return self.dollardollar
        if isinstance(t, tuple):
            if t[0] == 'num':
                return numval(t[1])
            name = t[1]
            if name not in self.syms:
                raise KeyError(name)
            return self.syms[name]
        raise ValueError(f'unexpected token {t!r}')


def eval_expr(s, syms, dollar=0, dollardollar=0):
    try:
        return ExprParser(tokenize_expr(s), syms, dollar,
                          dollardollar).parse()
    except KeyError as e:
        raise KeyError(str(e))


# ------------------------------------------------------------ operands ---

def split_operands(s):
    parts, depth, instr, cur = 0, False, False, ''
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == '"':
            instr = not instr
            cur += c
        elif not instr and c == '[':
            depth += 1
            cur += c
        elif not instr and c == ']':
            depth -= 1
            cur += c
        elif not instr and depth == 0 and c == ',':
            out.append(cur.strip())
            cur = ''
        else:
            cur += c
        i += 1
    if cur.strip():
        out.append(cur.strip())
    return out


def strip_size(op):
    m = re.match(r'(?i)^(byte|word|dword)\s+(.*)$', op)
    if m:
        return m.group(1).lower(), m.group(2).strip()
    return None, op


def parse_mem(inner, bits):
    """Returns ('direct', exprstr) or ('reg', name)."""
    inner = inner.strip()
    if re.match(r'^[A-Za-z]+$', inner) and reginfo(inner.lower()):
        return ('reg', inner.lower())
    return ('direct', inner)


def parse_operand(op, bits):
    op = op.strip()
    if op.startswith('[') and op.endswith(']'):
        _, inner = strip_size(op[1:-1])
        return ('mem',) + parse_mem(inner, bits)
    ri = reginfo(op.lower())
    if ri:
        return ('reg',) + ri
    if ':' in op and not op.startswith('"'):
        a, b = op.split(':', 1)
        return ('far', a.strip(), b.strip())
    return ('imm', op)


# --------------------------------------------------------------- dasm ---

class Item:
    pass


class Label(Item):
    def __init__(self, name, glob=False):
        self.name = name
        self.glob = glob
        self.addr = None


class Insn(Item):
    def __init__(self, mnem, operands, bits):
        self.mnem = mnem
        self.operands = operands
        self.bits = bits
        self.addr = None
        self.size = None
        self.rel8 = True  # branch relaxation state


class Data(Item):
    def __init__(self, kind, args, times=None):
        self.kind = kind  # db/dw/dq/align
        self.args = args
        self.times = times
        self.addr = None
        self.size = None


class Dasm:
    def __init__(self, fmt):
        self.fmt = fmt  # bin | win32
        self.bits = 16
        self.org = 0
        self.defines = {'__OUTPUT_FORMAT__': fmt}
        self.macros = {}
        self.items = []
        self.globs = set()
        self.externs = []  # ordered
        self.syms = {}     # name -> addr (labels)
        self.parent = None

    # -- preprocessing --
    def preprocess(self, lines):
        out = []
        cond = []  # stack of booleans (currently emitting?)
        in_macro = None
        for raw in lines:
            line = raw.split(';', 1)[0] if '"' not in raw else self._strip_comment(raw)
            line = line.rstrip()
            if not line.strip():
                continue
            s = line.strip()
            if in_macro is not None:
                if s.lower() == '%endmacro':
                    self.macros[in_macro[0]] = (in_macro[1], in_macro[2])
                    in_macro = None
                else:
                    in_macro[2].append(line)
                continue
            if s.lower().startswith('%macro'):
                parts = s.split()
                in_macro = [parts[1], int(parts[2]), []]
                continue
            if s.lower().startswith('%ifidn'):
                args = s[6:].strip()
                a, b = [x.strip() for x in args.split(',', 1)]
                a = self._expand_defs(a)
                b = self._expand_defs(b)
                cond.append(a.lower() == b.lower())
                continue
            if s.lower() == '%else':
                cond[-1] = not cond[-1]
                continue
            if s.lower() == '%endif':
                cond.pop()
                continue
            if cond and not all(cond):
                continue
            if s.lower().startswith('%define'):
                parts = s.split(None, 2)
                self.defines[parts[1]] = parts[2] if len(parts) > 2 else ''
                continue
            # macro invocation?
            head = s.split(None, 1)[0].rstrip(':')
            if head in self.macros and not s.rstrip().endswith(':'):
                nargs, body = self.macros[head]
                given = []
                if ' ' in s.strip() or '\t' in s.strip():
                    given = split_operands(s.strip().split(None, 1)[1])
                assert len(given) == nargs, f'macro {head} wants {nargs} args'
                for bl in body:
                    exp = bl
                    for idx, g in enumerate(given):
                        exp = exp.replace('%%{%d}' % (idx + 1), g)
                        exp = re.sub(r'%%%d(?![0-9])' % (idx + 1), g, exp)
                    out.append(exp)
                continue
            out.append(self._expand_defs(line))
        return out

    def _strip_comment(self, raw):
        instr = False
        for i, c in enumerate(raw):
            if c == '"':
                instr = not instr
            if c == ';' and not instr:
                return raw[:i]
        return raw

    def _expand_defs(self, line):
        for k in sorted(self.defines, key=len, reverse=True):
            line = re.sub(r'\b%s\b' % re.escape(k), self.defines[k], line)
        return line

    # -- parsing --
    def parse(self, lines):
        for line in lines:
            s = line.strip()
            if not s:
                continue
            if s.startswith('[') and s.endswith(']'):
                s = s[1:-1].strip()  # [BITS 32] / [ORG 0x7C00]
            low = s.lower()
            if low.startswith('bits '):
                self.bits = int(s.split()[1])
                continue
            if low.startswith('org '):
                self.org = numval(s.split()[1])
                continue
            if low.startswith('section '):
                continue  # single .text section
            if low.startswith('global '):
                for n in s.split(None, 1)[1].split():
                    self.globs.add(n.strip())
                    # ensure referenced later; mark on definition
                continue
            if low.startswith('extern '):
                for n in s.split(None, 1)[1].split():
                    n = n.strip()
                    if n not in self.externs:
                        self.externs.append(n)
                continue
            if low.startswith('times '):
                m = re.match(r'(?i)times\s+(.*?)\s+db\s+(.*)$', s)
                assert m, f'bad times: {s}'
                self.items.append(Data('timesdb', [m.group(2)], times=m.group(1)))
                continue
            if low.startswith('align '):
                self.items.append(Data('align', [s.split()[1]]))
                continue
            m = re.match(r'^([A-Za-z_.][\w.]*)\s*:\s*(.*)$', s)
            if m and not re.match(r'(?i)^(db|dw|dd|dq)\b', m.group(1)):
                name = m.group(1)
                rest = m.group(2).strip()
                if name.startswith('.'):
                    assert self.parent, f'orphan local label {name}'
                    name = self.parent + name
                else:
                    self.parent = name
                self.items.append(Label(name))
                s = rest
                if not s:
                    continue
            # label + data on one line: `msg db "...", 0`
            m = re.match(r'^([A-Za-z_.][\w.]*)\s+(db|dw|dd|dq)\s+(.*)$', s, re.I)
            if m:
                name = m.group(1)
                if name.startswith('.'):
                    assert self.parent, f'orphan local label {name}'
                    name = self.parent + name
                else:
                    self.parent = name
                self.items.append(Label(name))
                self.items.append(Data(m.group(2).lower(),
                                      split_operands(m.group(3))))
                continue
            m = re.match(r'(?i)^(db|dw|dd|dq)\s+(.*)$', s)
            if m:
                self.items.append(Data(m.group(1).lower(),
                                      split_operands(m.group(2))))
                continue
            parts = s.split(None, 1)
            mnem = parts[0].lower()
            ops = split_operands(parts[1]) if len(parts) > 1 else []
            if mnem == 'rep' and ops:
                mnem = 'rep_' + ops[0].lower()
                ops = []
            fixed = []
            for op in ops:
                if op.startswith('.'):
                    assert self.parent, f'orphan local ref {op}'
                    fixed.append(self.parent + op)
                else:
                    fixed.append(op)
            self.items.append(Insn(mnem, fixed, self.bits))

    # -- layout (pass 1 with branch relaxation) --
    def layout(self):
        for _ in range(20):
            if self._layout_once():
                return
        raise ValueError('layout did not converge')

    def _layout_once(self):
        off = 0
        syms = dict(self.syms)
        stable = True
        for it in self.items:
            if isinstance(it, Label):
                # symbol values are absolute (ORG-inclusive); win32 uses
                # ORG 0 so they double as section offsets there.
                if it.name not in syms or syms[it.name] != off + self.org:
                    stable = False
                syms[it.name] = off + self.org
                it.addr = off
            elif isinstance(it, Data):
                it.addr = off
                it.size = self._data_size(it, syms, off)
                off += it.size
            else:
                it.addr = off
                it.size = self._insn_size(it, syms, off)
                off += it.size
        changed = (syms != self.syms)
        self.syms = syms
        self.total = off
        return stable and not changed

    def _data_size(self, it, syms, off):
        if it.kind == 'align':
            n = eval_expr(it.args[0], syms, off + self.org, self.org)
            r = off % n
            return 0 if r == 0 else n - r
        if it.kind == 'timesdb':
            n = eval_expr(it.times, syms, off + self.org, self.org)
            return max(n, 0)
        total = 0
        for a in it.args:
            a = a.strip()
            if a.startswith('"'):
                total += len(a) - 2
            else:
                total += {'db': 1, 'dw': 2, 'dd': 4, 'dq': 8}[it.kind]
        return total

    def _branch_len(self, it, syms, off, short):
        return 2 if short else (6 if it.mnem in JCC else 5)

    def _regsize(self, P, i):
        p = P[i]
        assert p.get('t') == 'reg'
        return p.get('size', 32)

    def _prefix_len(self, sizes, bits):
        for s in sizes:
            if s != 0 and s != bits:
                return 1
        return 0

    def _insn_size(self, it, syms, off):
        # Pure form-based sizing (no values needed except branch relaxation
        # and imm-width choice; unknown values fall back safely).
        m, ops, bits = it.mnem, it.operands, it.bits
        P = self._parsed_ops(ops, bits)
        if m in JCC:
            if it.rel8:
                try:
                    tgt = self._eval_op(ops[0], syms, off)
                    if not -128 <= tgt - (off + self.org + 2) <= 127:
                        it.rel8 = False
                        return 4 if bits == 16 else 6
                except KeyError as e:
                    if e.args[0].strip("'") in self.externs:
                        it.rel8 = False
                        return 4 if bits == 16 else 6
            return 2 if it.rel8 else (4 if bits == 16 else 6)
        if m == 'jmp' and len(ops) == 1 and not self._is_far(ops[0]):
            if it.rel8:
                try:
                    tgt = self._eval_op(ops[0], syms, off)
                    if not -128 <= tgt - (off + self.org + 2) <= 127:
                        it.rel8 = False
                        return 3 if bits == 16 else 5
                except KeyError as e:
                    if e.args[0].strip("'") in self.externs:
                        it.rel8 = False
                        return 3 if bits == 16 else 5
            return 2 if it.rel8 else (3 if bits == 16 else 5)
        if m == 'call' and len(ops) == 1 and not self._is_far(ops[0]):
            return 3 if bits == 16 else 5
        if m == 'jmp' and len(ops) == 1 and self._is_far(ops[0]):
            return 5 if bits == 16 else 7
        if m in ('nop', 'cli', 'sti', 'hlt', 'cld', 'lodsb', 'ret',
                 'iret', 'pusha', 'popa', 'pushad', 'popad', 'rep_movsw'):
            return 1 if m != 'rep_movsw' else 2
        if m == 'int':
            return 2
        if m in ('in', 'out'):
            return 2
        if m == 'push':
            _, bare = strip_size(ops[0].strip())
            if reginfo(bare.lower()):
                ri = reginfo(bare.lower())
                return 2 if ri[0] == 's' and ri[2] >= 4 else 1
            try:
                v = eval_expr(bare, syms, off + self.org, self.org)
            except KeyError:
                return 5
            return 2 if -128 <= v <= 127 else 5
        if m == 'pop':
            ri = reginfo(ops[0].strip().lower())
            return 2 if ri[2] >= 4 else 1
        if m == 'lgdt':
            return 5
        if m == 'mov':
            d, s = P[0], P[1]
            pre = 0
            if d.get('t') == 'reg' and s.get('t') == 'imm':
                if d.get('k') == 'r' and d.get('size') == 8:
                    return 2
                if d.get('k') == 'r' and d.get('size') == 16:
                    return 3 + (1 if bits == 32 else 0)
                if d.get('k') == 'r' and d.get('size') == 32:
                    return 5 + (1 if bits == 16 else 0)
            if d.get('k') in ('s', 'c') or s.get('k') in ('s', 'c'):
                # segment moves: 2 bytes flat; control moves: fixed
                # 32-bit, 3 bytes, never prefixed (no 16-bit CR form)
                if d.get('k') == 'c' or s.get('k') == 'c':
                    return 3
                return 2
            # reg/reg/mem combos: opcode + modrm [+ disp/SIB] [+ 0x66]
            if d.get('t') == 'mem' or s.get('t') == 'mem':
                memop = ops[0] if d.get('t') == 'mem' else ops[1]
                other = s if d.get('t') == 'mem' else d
                inner = memop.strip()
                inner = inner[inner.find('[') + 1:inner.rfind(']')].strip()
                _, bare = strip_size(inner)
                n = 2
                if re.match(r'^[A-Za-z]+$', bare) and reginfo(bare.lower()):
                    # [ESP] alone needs a SIB byte; other [reg] don't.
                    if bits == 32 and reginfo(bare.lower())[2] == 4:
                        n += 1
                    else:
                        n += 0  # 16-bit [reg] (unused here)
                else:
                    n += 4 if bits == 32 else 2  # direct disp32/disp16
                if other.get('size', 0) == 16 and bits == 32:
                    n += 1  # 0x66 for 16-bit data in 32-bit mode
                return n
            n = 2
            if bits == 32 and 16 in (d.get('size', 0), s.get('size', 0)):
                n += 1
            return n
        if m in ('xor', 'or', 'and', 'add', 'sub', 'cmp', 'test'):
            d = P[0]
            if len(P) == 2 and P[1].get('t') == 'imm' and d.get('t') == 'reg':
                if d.get('size') == 8:
                    return 2 if (m == 'or' and d.get('code') == 0) else 3
                try:
                    v = eval_expr(strip_size(ops[1])[1], syms,
                                  off + self.org, self.org)
                except KeyError:
                    v = 0x7FFFFFFF
                n = 3 if -128 <= v <= 127 else (4 if d.get('size') == 16 else 6)
                if d.get('size') != bits:
                    n += 1
                return n
            n = 2
            if d.get('size') == 16 and bits == 32:
                n += 1
            return n
        if m in ('inc', 'dec'):
            n = 1
            if P[0].get('size') == 16 and bits == 32:
                n += 1
            return n
        if m in ('shl', 'shr'):
            return 3
        raise ValueError(f'cannot size {m} {ops} @ {off}')

    def _eval_op(self, op, syms, off):
        _, bare = strip_size(op.strip())
        return eval_expr(bare, syms, off + self.org, self.org)

    def _is_far(self, op):
        return ':' in op and not op.strip().startswith('"')

    # -- encoding --
    def need66(self, opsizes, bits):
        for s in opsizes:
            if s == 16 and bits == 32:
                return True
            if s == 32 and bits == 16:
                return True
        return False

    def encode(self, it, syms, off):
        m, ops, bits = it.mnem, it.operands, it.bits
        P = self._parsed_ops(ops, bits)
        out = bytearray()

        def regmodrm(regfield, rm):
            # rm: ('reg', kind, code) or ('mem-direct', val) or ('mem-reg', name)
            if rm[0] == 'reg':
                return bytes([(0xC0 | (regfield << 3) | rm[2])])
            if rm[0] == 'mem-direct':
                if bits == 16:
                    return bytes([0x06 | (regfield << 3)]) + struct.pack('<H', rm[1] & 0xFFFF)
                return bytes([0x05 | (regfield << 3)]) + struct.pack('<I', rm[1] & 0xFFFFFFFF)
            if rm[0] == 'mem-reg':
                assert bits == 32
                code = REGS32[rm[1]]
                return bytes([0x07, 0x24 | (code << 3) if False else (0x00 | (4 << 0))]) \
                    + bytes([(0 << 6) | (4 << 3) | code])
            raise ValueError('bad rm')

        pre = b'\x66' if self.need66([p.get('size', 0) for p in P], bits) else b''
        if m == 'nop':
            return b'\x90'
        if m in ('cli', 'sti', 'hlt', 'cld', 'lodsb', 'ret', 'iret',
                 'pusha', 'popa', 'pushad', 'popad'):
            return {'cli': b'\xfa', 'sti': b'\xfb', 'hlt': b'\xf4',
                    'cld': b'\xfc', 'lodsb': b'\xac', 'ret': b'\xc3',
                    'iret': b'\xcf', 'pusha': b'\x60',
                    'popa': b'\x61', 'pushad': b'\x60',
                    'popad': b'\x61'}[m]
        if m == 'rep_movsw':
            return b'\xF3\xA5'
        if m == 'int':
            return bytes([0xCD, self._imm(ops[0], syms, off, 8)])
        if m == 'in' and P[0].get('t') == 'reg' and P[0].get('k') == 'r' \
                and P[0].get('size') == 8 and P[0].get('code') == 0:
            return bytes([0xE4, self._imm(ops[1], syms, off, 8)])
        if m == 'out' and P[1].get('t') == 'reg' and P[1].get('k') == 'r' \
                and P[1].get('size') == 8 and P[1].get('code') == 0:
            return bytes([0xE6, self._imm(ops[0], syms, off, 8)])
        if m == 'push':
            return self._enc_push(ops[0], syms, off, bits)
        if m == 'pop':
            return self._enc_pop(ops[0], bits)
        if m == 'call' and len(ops) == 1 and not self._is_far(ops[0]):
            tgt = self._imm(ops[0], syms, off, 32, True)
            if isinstance(tgt, tuple):  # external
                self._reloc(off + 1, tgt[1])
                if bits == 16:
                    return b'\xE8\x00\x00'
                return b'\xE8\x00\x00\x00\x00'
            if bits == 16:
                return b'\xE8' + struct.pack('<h', tgt - (off + self.org + 3))
            return b'\xE8' + struct.pack('<i', tgt - (off + self.org + 5))
        if m == 'jmp' and len(ops) == 1 and not self._is_far(ops[0]):
            tgt = self._imm(ops[0], syms, off, 32, True)
            if isinstance(tgt, tuple):
                self._reloc(off + 1, tgt[1])
                return b'\xE9\x00\x00\x00\x00'
            ln = self._branch_len(it, syms, off, it.rel8)
            if ln == 2:
                d = tgt - (off + self.org + 2)
                assert -128 <= d <= 127, f'jmp rel8 out of range @ {off}'
                return b'\xEB' + struct.pack('b', d)
            if bits == 16:
                return b'\xE9' + struct.pack('<h', tgt - (off + self.org + 3))
            return b'\xE9' + struct.pack('<i', tgt - (off + self.org + 5))
        if m == 'jmp' and len(ops) == 1 and self._is_far(ops[0]):
            seg, o = [x.strip() for x in ops[0].split(':', 1)]
            segv = self._imm(seg, syms, off, 16)
            offv = self._imm(o, syms, off, 32)
            if bits == 16:
                return b'\xEA' + struct.pack('<H', offv & 0xFFFF) + struct.pack('<H', segv & 0xFFFF)
            return b'\xEA' + struct.pack('<I', offv & 0xFFFFFFFF) + struct.pack('<H', segv & 0xFFFF)
        if m in JCC:
            tgt = self._imm(ops[0], syms, off, 32, True)
            if isinstance(tgt, tuple):
                raise ValueError('external jcc not supported')
            if it.rel8:
                d = tgt - (off + self.org + 2)
                assert -128 <= d <= 127, f'jcc rel8 out of range @ {off}'
                return bytes([JCC[m]]) + struct.pack('b', d)
            return bytes([0x0F, JCC32[m]]) + struct.pack('<i', tgt - (off + self.org + 6))
        if m == 'lgdt':
            assert ops and ops[0].strip().startswith('[')
            mem = self._memval(ops[0], syms, off, bits)
            if mem[0] == 'ext':
                raise ValueError('lgdt of external?')
            out = pre + b'\x0F\x01'
            return out + self._modrm_mem(2, mem, bits)
        if m == 'lidt':
            raise ValueError('lidt not in subset')
        if m == 'mov':
            # segment/control moves have fixed size: no 0x66 ever
            pmov = b'' if any(p.get('k') in ('s', 'c') for p in P) else pre
            return pmov + self._enc_mov(P, ops, syms, off, bits)
        if m == 'xor':
            return pre + self._enc_alu(0x30, P, ops, syms, off, bits)
        if m == 'or':
            if (len(P) == 2 and P[0].get('t') == 'reg' and P[0].get('k') == 'r'
                    and P[1].get('t') == 'imm'):
                v = self._imm(ops[1], syms, off, 32)
                size = P[0]['size']
                if size == 8 and P[0]['code'] == 0:
                    return pre + bytes([0x0C, v & 0xFF])
                return pre + self._alu_imm(0x81, 0x83, 1, P[0], v, bits)
            return pre + self._enc_alu(0x08, P, ops, syms, off, bits)
        if m == 'and':
            return pre + self._enc_alu(0x20, P, ops, syms, off, bits)
        if m == 'add':
            if len(P) == 2 and P[0].get('t') == 'reg':
                v = self._imm(ops[1], syms, off, 32)
                return pre + self._alu_imm(0x81, 0x83, 0, P[0], v, bits)
            return pre + self._enc_alu(0x00, P, ops, syms, off, bits)
        if m == 'sub':
            if len(P) == 2 and P[0].get('t') == 'reg':
                v = self._imm(ops[1], syms, off, 32)
                return pre + self._alu_imm(0x81, 0x83, 5, P[0], v, bits)
            return pre + self._enc_alu(0x28, P, ops, syms, off, bits)
        if m == 'cmp':
            return pre + self._enc_alu(0x38, P, ops, syms, off, bits)
        if m == 'test':
            return pre + self._enc_alu(0x84, P, ops, syms, off, bits)
        if m == 'inc':
            assert P[0].get('t') == 'reg' and P[0].get('k') == 'r'
            return pre + bytes([0x40 + P[0]['code']])
        if m == 'dec':
            assert P[0].get('t') == 'reg' and P[0].get('k') == 'r'
            return pre + bytes([0x48 + P[0]['code']])
        if m == 'shl':
            return pre + self._enc_shift(4, P, ops, syms, off, bits)
        if m == 'shr':
            return pre + self._enc_shift(5, P, ops, syms, off, bits)
        raise ValueError(f'unsupported: {m} {ops}')

    # -- operand helpers --
    def _parsed_ops(self, ops, bits):
        out = []
        for op in ops:
            k = parse_operand(op, bits)
            if k[0] == 'reg':
                out.append({'t': 'reg', 'k': k[1], 'size': k[2], 'code': k[3]})
            elif k[0] == 'mem':
                out.append({'t': 'mem', 'm': k[1:]})
            elif k[0] == 'imm':
                out.append({'t': 'imm'})
            elif k[0] == 'far':
                out.append({'t': 'far'})
        return out

    def _imm(self, op, syms, off, nbits, relok=False):
        op = op.strip()
        _, bare = strip_size(op)
        try:
            return eval_expr(bare, syms, off + self.org, self.org)
        except KeyError as e:
            name = e.args[0].strip("'")
            if relok and name in self.externs:
                return ('ext', name)
            raise

    def _memval(self, op, syms, off, bits):
        op = op.strip()
        assert op.startswith('[') and op.endswith(']')
        _, inner = strip_size(op[1:-1])
        inner = inner.strip()
        if re.match(r'^[A-Za-z]+$', inner) and reginfo(inner.lower()):
            return ('reg', inner.lower())
        try:
            return ('abs', eval_expr(inner, syms, off + self.org, self.org))
        except KeyError as e:
            name = e.args[0].strip("'")
            if name in self.externs:
                return ('ext', name)
            raise

    def _modrm_mem(self, regfield, mem, bits):
        if mem[0] == 'abs':
            v = mem[1]
            if bits == 16:
                return bytes([0x06 | (regfield << 3)]) + struct.pack('<H', v & 0xFFFF)
            return bytes([0x05 | (regfield << 3)]) + struct.pack('<I', v & 0xFFFFFFFF)
        if mem[0] == 'reg':
            code = REGS32[mem[1]]
            if code == 4:  # [ESP] alone needs a SIB byte
                return bytes([0x04 | (regfield << 3), 0x24])
            return bytes([(regfield << 3) | code])
        raise ValueError('bad mem for modrm')

    def _enc_push(self, op, syms, off, bits):
        op = op.strip()
        _, bare = strip_size(op)
        ri = reginfo(bare.lower())
        if ri:
            if ri[0] == 's':
                return {0: b'\x06', 1: b'\x0E', 2: b'\x16', 3: b'\x1E',
                        4: b'\x0F\xA0', 5: b'\x0F\xA8'}[ri[2]]
            return bytes([0x50 + ri[2]])
        v = self._imm(op, syms, off, 32)
        if -128 <= v <= 127:
            return bytes([0x6A, v & 0xFF])
        return b'\x68' + struct.pack('<i', v)

    def _enc_pop(self, op, bits):
        ri = reginfo(op.strip().lower())
        assert ri and ri[0] == 's', f'pop {op!r} not in subset'
        return {0: b'\x07', 1: b'\x0F', 2: b'\x17', 3: b'\x1F',
                4: b'\x0F\xA1', 5: b'\x0F\xA9'}[ri[2]]

    def _enc_mov(self, P, ops, syms, off, bits):
        d, s = P[0], P[1]
        if d['t'] == 'reg' and s['t'] == 'imm':
            v = self._imm(ops[1], syms, off, 32)
            if d['k'] == 'r' and d['size'] == 8:
                return bytes([0xB0 + d['code'], v & 0xFF])
            if d['k'] == 'r' and d['size'] == 16:
                return bytes([0xB8 + d['code']]) + struct.pack('<H', v & 0xFFFF)
            if d['k'] == 'r' and d['size'] == 32:
                return bytes([0xB8 + d['code']]) + struct.pack('<I', v & 0xFFFFFFFF)
        if d['t'] == 'reg' and s['t'] == 'reg':
            if d['k'] == 's' and s['k'] == 'r' and s['size'] == 16:
                return bytes([0x8E, 0xC0 | (d['code'] << 3) | s['code']])
            if d['k'] == 'r' and s['k'] == 'r' and d['size'] == s['size']:
                sz = d['size']
                return bytes([(0x88 if sz == 8 else 0x89),
                              0xC0 | (s['code'] << 3) | d['code']])
        if d['t'] == 'mem' and s['t'] == 'reg' and s['k'] == 'r':
            mem = self._memval(ops[0], syms, off, bits)
            if isinstance(mem, tuple) and mem[0] == 'ext':
                raise ValueError('mem of external?')
            opc = 0x88 if s['size'] == 8 else 0x89
            return bytes([opc]) + self._modrm_mem(s['code'], mem, bits)
        if d['t'] == 'reg' and d['k'] == 'r' and s['t'] == 'mem':
            mem = self._memval(ops[1], syms, off, bits)
            opc = 0x8A if d['size'] == 8 else 0x8B
            return bytes([opc]) + self._modrm_mem(d['code'], mem, bits)
        if d['t'] == 'reg' and d['k'] == 'r' and s['t'] == 'reg' and s['k'] == 'c':
            return bytes([0x0F, 0x20, 0xC0 | (s['code'] << 3) | d['code']])
        if d['t'] == 'reg' and d['k'] == 'c' and s['t'] == 'reg':
            return bytes([0x0F, 0x22, 0xC0 | (d['code'] << 3) | s['code']])
        raise ValueError(f'mov combo not in subset: {ops}')

    def _enc_alu(self, base, P, ops, syms, off, bits):
        # base = r/m8,r8 opcode (ADD 0x00, OR 0x08, ..., TEST 0x84).
        # NASM emits r/m,r direction: 8-bit -> base+0, 16/32 -> base+1.
        d, s = P[0], P[1]
        if d.get('t') == 'reg' and d.get('k') == 'r' \
                and s.get('t') == 'reg' and s.get('k') == 'r' \
                and d.get('size') == s.get('size'):
            size = d.get('size')
            opc = base + (0 if size == 8 else 1)
            return bytes([opc, 0xC0 | (s.get('code') << 3) | d.get('code')])
        raise ValueError(f'alu combo not in subset: {ops}')

    def _alu_imm(self, op81, op83, ext, regp, v, bits):
        size = regp['size']
        if size == 8:
            return bytes([0x80 | 0, 0xC0 | (ext << 3) | regp['code'], v & 0xFF])
        if -128 <= v <= 127:
            return bytes([op83, 0xC0 | (ext << 3) | regp['code'], v & 0xFF])
        fmt = '<h' if size == 16 else '<i'
        return bytes([op81, 0xC0 | (ext << 3) | regp['code']]) + struct.pack(fmt, v)

    def _enc_shift(self, ext, P, ops, syms, off, bits):
        assert P[0]['t'] == 'reg' and P[1]['t'] == 'imm'
        v = self._imm(ops[1], syms, off, 8)
        size = P[0]['size']
        if v == 1:
            return bytes([(0xD0 if size == 8 else 0xD1),
                          0xC0 | (ext << 3) | P[0]['code']])
        return bytes([(0xC0 if size == 8 else 0xC1),
                      0xC0 | (ext << 3) | P[0]['code'], v & 0xFF])

    def _reloc(self, off, name):
        self.relocs.append((off, name))

    # -- data emission --
    def emit_data(self, it, syms, off):
        out = bytearray()
        if it.kind == 'align':
            n = eval_expr(it.args[0], syms, off + self.org, self.org)
            r = off % n
            if r:
                out += b'\x90' * (n - r)
            return bytes(out)
        if it.kind == 'timesdb':
            n = eval_expr(it.times, syms, off + self.org, self.org)
            assert n >= 0, 'negative times'
            return b'\x00' * n
        for a in it.args:
            a = a.strip()
            if a.startswith('"'):
                out += a[1:-1].encode('latin-1')
                continue
            mch = re.match(r"^'(.)'$", a)
            if mch:
                out += mch.group(1).encode('latin-1')
                continue
            v = eval_expr(a, syms, off + self.org, self.org)
            if it.kind == 'db':
                out += struct.pack('B', v & 0xFF)
            elif it.kind == 'dw':
                out += struct.pack('<H', v & 0xFFFF)
            elif it.kind == 'dd':
                out += struct.pack('<I', v & 0xFFFFFFFF)
            elif it.kind == 'dq':
                out += struct.pack('<Q', v & 0xFFFFFFFFFFFFFFFF)
            else:
                raise ValueError(f'bad data kind {it.kind}')
        return bytes(out)

    # -- assemble driver --
    def assemble(self):
        self.layout()
        code = bytearray()
        self.relocs = []
        off = 0
        for it in self.items:
            if isinstance(it, Label):
                continue
            if isinstance(it, Data):
                code += self.emit_data(it, self.syms, off)
                off += it.size
            else:
                chunk = self.encode(it, self.syms, off)
                assert len(chunk) == it.size, \
                    f'{it.mnem} size drift {len(chunk)}!={it.size} @ {off}'
                code += chunk
                off += it.size
        assert off == self.total
        if self.fmt == 'bin':
            return bytes(code), []
        return bytes(code), list(self.relocs)

    # -- COFF writer (win32, NASM-compatible) --
    def write_coff(self, code, relocs, path):
        # symbol order: .file, .text(+aux), .absolut, appearance, @feat.00
        order = []
        seen = set()

        def add(name):
            if name not in seen:
                seen.add(name)
                order.append(name)
        for n in self.externs:
            add(n)
        for it in self.items:
            if isinstance(it, Label):
                lname = it.name if it.name in self.globs else None
                # defined globals + all locals (static) get entries
                if it.name in self.globs or it.name not in self.externs:
                    add(it.name)
        # string table
        strtab = bytearray(b'\x00\x00\x00\x00')
        stroff = {}

        def symref(name):
            if len(name) <= 8:
                return name.encode('latin-1').ljust(8, b'\x00')
            if name not in stroff:
                stroff[name] = 4 + len(strtab) - 4
                strtab.extend(name.encode('latin-1') + b'\x00')
            return struct.pack('<II', 0, stroff[name])

        syms = bytearray()
        # .file (path truncated to 18, like NASM)
        syms += struct.pack('<8sIhHBB', b'.file\x00\x00\x00', 0, -2, 0, 103, 1)
        syms += path.encode('latin-1')[:18].ljust(18, b'\x00')
        # .text + aux
        syms += struct.pack('<8sIhHBB', b'.text\x00\x00\x00', 0, 1, 0, 3, 1)
        syms += struct.pack('<III', len(code), len(relocs), 0) + b'\x00' * 6
        # .absolut
        syms += struct.pack('<8sIhHBB', b'.absolut', 0, -1, 0, 3, 0)
        idx = {}
        # COFF indices count aux records: .file=0, .file-aux=1,
        # .text=2, .text-aux=3, .absolut=4, then appearance order.
        nxt = [5]
        for n in order:
            idx[n] = nxt[0]
            nxt[0] += 1
            if n in self.externs:
                syms += symref(n) + struct.pack('<IhHBB', 0, 0, 0, 2, 0)
            else:
                cls = 2 if n in self.globs else 3
                syms += symref(n) + struct.pack('<IhHBB', self.syms[n], 1, 0, cls, 0)
        # @feat.00
        syms += struct.pack('<8sIhHBB', b'@feat.00', 1, -1, 0, 3, 0)
        nsym = 6 + len(order)
        # fix strtab length
        strtab[0:4] = struct.pack('<I', len(strtab))
        # relocs
        rsec = bytearray()
        for off, name in relocs:
            rsec += struct.pack('<IIH', off, idx[name], 0x14)
        # layout: headers(60) + code + relocs + syms + strtab
        rawptr = 60
        relptr = rawptr + len(code)
        symptr = relptr + len(rsec)
        hdr = struct.pack('<HHIIIHH', 0x14C, 1, 0, symptr, nsym, 0, 0)
        sh = (b'.text\x00\x00\x00' + struct.pack('<II', 0, 0)
              + struct.pack('<I', len(code)) + struct.pack('<I', rawptr)
              + struct.pack('<I', relptr) + struct.pack('<I', 0)
              + struct.pack('<H', len(relocs)) + struct.pack('<H', 0)
              + struct.pack('<I', 0x60500020))
        return hdr + sh + bytes(code) + bytes(rsec) + bytes(syms) + bytes(strtab)


def main():
    args = sys.argv[1:]
    fmt, out, src, expect = None, None, None, None
    i = 0
    while i < len(args):
        if args[i] == '-f':
            fmt = args[i + 1]
            i += 2
        elif args[i] == '-o':
            out = args[i + 1]
            i += 2
        elif args[i] == '--expect':
            expect = args[i + 1]
            i += 2
        else:
            src = args[i]
            i += 1
    assert fmt in ('bin', 'win32'), 'need -f bin|win32'
    assert src, 'need input'
    if out is None:
        out = 'a.out'
    text = open(src).read().splitlines()
    d = Dasm(fmt)
    stmts = d.preprocess(text)
    d.parse(stmts)
    code, relocs = d.assemble()
    if fmt == 'bin':
        open(out, 'wb').write(code)
    else:
        open(out, 'wb').write(d.write_coff(code, relocs, src))
    print(f'eoscwa: {src} -> {out} ({len(code)} bytes, {len(relocs)} relocs)')
    if expect:
        a = bytearray(open(expect, 'rb').read())
        b = bytearray(open(out, 'rb').read())
        if fmt == 'win32':
            a[4:8] = b'\x00\x00\x00\x00'  # COFF timestamp differs per build
            b[4:8] = b'\x00\x00\x00\x00'
        if bytes(a) != bytes(b):
            n = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
            print(f'eoscwa: MISMATCH vs {expect} ({n} bytes differ)')
            sys.exit(1)
        print(f'eoscwa: identical to {expect}')


if __name__ == '__main__':
    main()
