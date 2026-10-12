#!/usr/bin/env python3
"""Generate an offline interactive ROM report from the user's ROM and analysis.

Usage: python3 tools/rom_report.py [--rom rom/sml.gb] [--output build/rom-report.html]
Generated reports contain ROM bytes and must stay out of version control.
"""
import argparse
import base64
import collections
import hashlib
import json
import re
from pathlib import Path
import sm83
import symbols

CATEGORIES = ['Unknown', 'Code · unassigned', 'Code · audio', 'Code · game logic',
              'Code · graphics / video', 'Code · input', 'Code · HUD / score',
              'Code · system / utilities', 'Data · graphics / tiles',
              'Data · music / sound', 'Data · levels / enemies',
              'Data · pointers / tables', 'Data · text', 'Data · other', 'Cartridge header']
COLORS = ['#354252', '#8b9cb3', '#b18cff', '#56d6ac', '#66baff', '#ff92c2', '#f2c66d',
          '#d5e3ee', '#337bb8', '#744fb5', '#27896f', '#ac803c', '#b4547c', '#697461', '#e97659']


def offset(bank, addr):
    return addr if addr < 0x4000 else bank * 0x4000 + addr - 0x4000


def key(bank, addr):
    return '%02X:%04X' % (bank if addr >= 0x4000 else 0, addr)


def task(name, bank, addr, text=''):
    if text:
        category, reason = task(name, bank, addr)
        if category != 1:
            return category, reason
    s = (name + ' ' + text).lower()
    if bank == 3 and addr >= 0x6600 or bank == 0 and addr == 0x50 or re.search(r'music|sound|sfx|stereo|channel|square|noise|wave', s):
        return 2, 'Audio engine range / sound symbol'
    if bank == 0 and addr in (0x40, 0x48) or re.search(r'vblank|lcdstatus', s):
        return 4, 'Video interrupt entry / symbol'
    if re.search(r'joypad|input|button', s):
        return 5, 'Input symbol / source description'
    if re.search(r'hud|score|lives|timer|coin', s):
        return 6, 'HUD / score symbol or source description'
    if re.search(r'draw|display|vblank|lcd|video|scroll|background|sprite|tile|vram', s):
        return 4, 'Video symbol / source description'
    if re.search(r'gamestate|mario|player|enemy|enemies|collision|level|kill|injure|pause|object|superball', s):
        return 3, 'Game symbol / source description'
    if re.search(r'copy|fill|init|tablejump|random|bank|reset|interrupt', s):
        return 7, 'System / utility symbol'
    return 1, 'Task not established'


def build(args):
    rom = Path(args.rom).read_bytes()
    if len(rom) != 65536:
        raise ValueError('This 256 × 256 atlas requires the 64 KiB Super Mario Land ROM')
    asm = symbols.load(rom, args.disasm)
    if asm.errors:
        raise ValueError('Disassembly does not match this ROM; use the v1.1 ROM')
    cov = Path(args.coverage).read_bytes()
    if len(cov) != len(rom):
        raise ValueError('Coverage length does not match ROM')
    labels = collections.defaultdict(list)
    for n, (b, a) in asm.symbols.items():
        if a < 0x8000:
            labels[offset(b, a)].append(n)
    source = {}
    cats = bytearray(len(rom))
    region = [''] * len(rom)
    current = ''
    for b, a, size, kind, text, comment, where in asm.lines:
        o = offset(b, a)
        globals_here = [n for n in labels[o] if '.' not in n]
        if globals_here:
            current = globals_here[0]
        description = ' · '.join(x for x in (current, text, comment, where) if x)
        cat = 0
        if kind == 'code':
            cat = task(current, b, a, comment)[0]
        elif kind == 'incbin' and 'baserom.gb' in text:
            cat = 0  # raw ROM inclusion is not evidence of data
        elif kind in ('data', 'incbin'):
            s = (current + ' ' + text + ' ' + where).lower()
            if 'gfx/' in s or '.2bpp' in s:
                cat = 8
            elif 'music' in s or 'sound' in s or b == 3 and a >= 0x6600:
                cat = 9
            elif 'pointer' in s or text.startswith('dw'):
                cat = 11
            elif 'level' in s or 'enemy' in s:
                cat = 10
            elif '"' in text and kind == 'data':
                cat = 12
            else:
                cat = 13
        for i in range(o, min(o + size, len(rom))):
            cats[i] = cat
            region[i] = description
        if kind == 'code':
            source[o] = (size, text, comment, where)
    # Coverage bit 1 marks instruction starts; include their operand bytes.
    for o, flag in enumerate(cov):
        if flag & 1:
            ins = sm83.decode(rom[o:o+3] + b'\0\0', o % 0x4000 + (0x4000 if o >= 0x4000 else 0))
            for i in range(o, min(o + ins.length, len(rom))):
                if cats[i] == 0 or cats[i] >= 8:
                    cats[i] = 1
    cats[0x104:0x150] = bytes([14]) * 0x4c
    edges = []
    entries = set()
    stats = collections.defaultdict(lambda: {'count': 0, 'kinds': set()})
    for line in Path(args.callgraph).read_text().splitlines():
        kind, src, dst, count = line.split()
        edges.append((kind, src, dst, int(count)))
        if kind in ('call', 'rst', 'jphl', 'irq'):
            b, a = (int(x, 16) for x in dst.split(':'))
            if a < 0x8000 and offset(b, a) < len(rom):
                entries.add(dst)
                stats[dst]['count'] += int(count)
                stats[dst]['kinds'].add(kind)
    # Also include statically named call targets and named code entries.
    for o, (_, text, _, _) in source.items():
        for n in re.findall(r'[A-Za-z_][\w.]*', text):
            if text.startswith(('call ', 'jp ')) and n in asm.symbols:
                b, a = asm.symbols[n]
                if offset(b, a) in source and a < 0x8000:
                    entries.add(key(b, a))
    for o, ns in labels.items():
        if o in source and any('.' not in n for n in ns):
            entries.add(key(o // 0x4000, o if o < 0x4000 else 0x4000 + o % 0x4000))
    functions = []
    owners = collections.defaultdict(list)
    for entry in sorted(entries):
        bank, start = (int(x, 16) for x in entry.split(':'))
        todo, seen, byte_set, instructions, exits, callees = [start], set(), set(), [], [], set()
        while todo:
            pc = todo.pop()
            o = offset(bank, pc)
            if pc in seen or not 0 <= pc < 0x8000 or not 0 <= o < len(rom):
                continue
            if pc != start and key(bank, pc) in entries:
                exits.append('Tail jump → ' + key(bank, pc))
                continue
            if o not in source and not cov[o] & 1:
                exits.append('Unresolved / unverified flow at ' + key(bank, pc))
                continue
            seen.add(pc)
            ins = sm83.decode(rom[o:o+3] + b'\0\0', pc)
            size, text, comment, where = source.get(o, (ins.length, ins.text, '', 'Dynamic execution'))
            byte_set.update(range(o, min(o + ins.length, len(rom))))
            instructions.append({'address': key(bank, pc), 'offset': o, 'bytes': rom[o:o+ins.length].hex(' ').upper(),
                                 'text': text, 'comment': comment, 'source': where, 'executed': bool(cov[o] & 1)})
            nxt = pc + ins.length
            if ins.flow in (sm83.JP, sm83.JR):
                todo.append(ins.target)
                if ins.cond:
                    todo.append(nxt)
            elif ins.flow in (sm83.CALL, sm83.RST):
                callees.add(key(bank, ins.target))
                if ins.flow == sm83.RST and ins.target == 0x28:
                    exits.append('Computed jump table at ' + key(bank, pc))
                else:
                    todo.append(nxt)
            elif ins.flow in (sm83.RET, sm83.RETI):
                exits.append(('Return from interrupt' if ins.flow == sm83.RETI else 'Return') + ' at ' + key(bank, pc))
                if ins.cond:
                    todo.append(nxt)
            elif ins.flow in (sm83.JPHL, sm83.ILLEGAL):
                exits.append('Computed / unresolved exit at ' + key(bank, pc))
            else:
                todo.append(nxt)
        ns = labels[offset(bank, start)]
        name = next((n for n in ns if '.' not in n), 'Routine ' + entry)
        comment = source.get(offset(bank, start), (0, '', '', ''))[2]
        category, reason = task(name, bank, start, comment)
        ix = len(functions)
        ranges = []
        for o in sorted(byte_set):
            owners[o].append(ix)
            if cats[o] < 8 and category != 1:
                cats[o] = category
            if ranges and ranges[-1][0] + ranges[-1][1] == o:
                ranges[-1][1] += 1
            else:
                ranges.append([o, 1])
        functions.append({'id': entry, 'name': name, 'aliases': ns, 'task': category, 'reason': reason,
                          'comment': comment, 'ranges': ranges, 'bytes': len(byte_set),
                          'instructions': sorted(instructions, key=lambda x: x['offset']),
                          'executed': sum(i['executed'] for i in instructions),
                          'count': stats[entry]['count'], 'kinds': sorted(stats[entry]['kinds']),
                          'callees': sorted(callees), 'exits': sorted(set(exits)), 'callers': [], 'call_sites': []})
    for kind, src, dst, count in edges:
        if kind in ('call', 'rst', 'jphl', 'irq'):
            b, a = (int(x, 16) for x in src.split(':'))
            for f in functions:
                if f['id'] == dst:
                    os = owners.get(offset(b, a), [])
                    site_off = offset(b, a)
                    if kind != 'irq' and 0 <= site_off < len(rom):
                        site_ins = sm83.decode(rom[site_off:site_off+3] + b'\0\0', a)
                        site_range = [site_off, min(site_ins.length, len(rom)-site_off)]
                        if site_range not in f['call_sites']:
                            f['call_sites'].append(site_range)
                    f['callers'].append({'from': [functions[i]['id'] for i in os] or [src], 'site': src, 'kind': kind, 'count': count})
    # Add source-visible calling instructions even when never executed.
    f_by_id = {f['id']: f for f in functions}
    for site_off in source:
        bank = site_off // 0x4000
        addr = site_off if bank == 0 else 0x4000 + site_off % 0x4000
        ins = sm83.decode(rom[site_off:site_off+3] + b'\0\0', addr)
        if ins.flow not in (sm83.CALL, sm83.RST):
            continue
        target = key(bank, ins.target)
        if target in f_by_id:
            f = f_by_id[target]
            site_range = [site_off, ins.length]
            if site_range not in f['call_sites']:
                f['call_sites'].append(site_range)
                f['callers'].append({'from': [functions[i]['id'] for i in owners.get(site_off, [])] or [key(bank, addr)],
                                     'site': key(bank, addr), 'kind': 'static ' + ('call' if ins.flow == sm83.CALL else 'rst'), 'count': 0})
    descriptions, indices, lookup = [], [], {}
    for s in region:
        if s not in lookup:
            lookup[s] = len(descriptions)
            descriptions.append(s)
        indices.append(lookup[s])
    data = {'size': len(rom), 'sha256': hashlib.sha256(rom).hexdigest(), 'rom': base64.b64encode(rom).decode(),
            'categories': CATEGORIES, 'colors': COLORS, 'cats': base64.b64encode(cats).decode(),
            'coverage': base64.b64encode(cov).decode(), 'functions': functions,
            'descriptions': descriptions, 'regions': indices, 'owners': dict(owners),
            'counts': dict(collections.Counter(cats)), 'checked': asm.checked}
    template = Path(__file__).with_name('rom_report.html').read_text()
    out = Path(args.output)
    # A standalone report contains the entire ROM. Keep it in ignored build/.
    build_dir = Path(__file__).resolve().parents[1] / 'build'
    if not out.resolve().is_relative_to(build_dir):
        raise ValueError('ROM reports must be written under the ignored build/ directory')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(template.replace('__REPORT_DATA__', json.dumps(data, separators=(',', ':')).replace('<', '\\u003c')))
    print(f'{out}: {len(functions)} functions; {len(rom):,} bytes; {asm.checked:,} source instructions verified')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rom', default='rom/sml.gb')
    p.add_argument('--disasm', default='reference/disasm')
    p.add_argument('--coverage', default='build/coverage.bin')
    p.add_argument('--callgraph', default='build/callgraph.txt')
    p.add_argument('--output', default='build/rom-report.html')
    build(p.parse_args())
