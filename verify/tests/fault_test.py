#!/usr/bin/env python3
"""Exercise the Linux crash relay without sending any network requests."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fault-test-', dir=root / 'build') as tmp:
    directory = Path(tmp)
    fake = directory / 'curl'
    fake.write_text('#!/usr/bin/env python3\nimport sys,json,os\n'
                    'with open(os.environ["FAULT_TEST_OUTPUT"],"a") as f:\n'
                    ' f.write(json.dumps(sys.argv[1:])+"\\n")\n')
    fake.chmod(0o755)
    test = directory / 'test.c'
    test.write_text('#include "fault.h"\n#include <signal.h>\n#include <string.h>\n'
                    'int main(int n,char **v){fault_install(); '
                    'if(n>1 && !strcmp(v[1],"crash")) raise(SIGABRT); '
                    'if(n>1 && !strcmp(v[1],"fault")){fault_report();fault_report();}'
                    'return 0;}\n')
    binary = directory / 'test'
    subprocess.run([os.environ.get('CC', 'cc'), '-Wall', '-Wextra', '-Isrc/platform',
                    'src/platform/fault.c', str(test), '-o', str(binary)], cwd=root, check=True)
    output = directory / 'argv.jsonl'
    env = dict(os.environ, PATH=str(directory) + ':' + os.environ['PATH'],
               FAULT_TEST_OUTPUT=str(output))
    env.pop('SML_NO_REPORT', None)
    for mode, disabled, expected in [('normal', False, 0), ('fault', False, 1),
                                      ('crash', False, 1), ('fault', True, 0)]:
        output.unlink(missing_ok=True)
        current = dict(env)
        if disabled:
            current['SML_NO_REPORT'] = '1'
        started = time.monotonic()
        result = subprocess.run([str(binary), mode], env=current, check=False)
        assert time.monotonic() - started < 1, 'Game waited on reporting'
        if expected:
            deadline = time.monotonic() + 3
            while not output.exists() and time.monotonic() < deadline:
                time.sleep(.02)
        time.sleep(.1)
        rows = [json.loads(line) for line in output.read_text().splitlines()] if output.exists() else []
        assert len(rows) == expected, (mode, disabled, rows)
        if rows:
            payload = rows[0][rows[0].index('-d') + 1]
            assert payload.startswith('super-mario-land-port M0-dev Linux ')
            assert payload.endswith(' application fault')
            assert '--max-time' in rows[0]
        assert result.returncode == (134 if mode == 'crash' else 0)
    print('fault relay: normal exit, fatal fault, signal, duplicate suppression and opt-out passed')
