"""Exercise the built Windows launcher with an isolated library, not the user's data."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def verify(executable):
    executable = Path(executable).resolve()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='desktop-startup-', dir=root / '.cache') as temp:
        directory = Path(temp)
        data = directory / 'Library with spaces'
        data.mkdir()
        (data / 'settings.ini').write_text(
            '[windows]\nshowFloat=false\nshowLyrics=false\n'
            '[lyrics]\nautoFetch=false\n[application]\ncloseToTray=false\n', encoding='utf-8')
        env = dict(os.environ)
        system_root = os.environ.get('SystemRoot', 'C:/Windows')
        env['PATH'] = os.pathsep.join([str(Path(system_root) / 'System32'), system_root])
        with (directory / 'process.log').open('wb') as log:
            first = subprocess.Popen([str(executable), '--startup', '--data', str(data)],
                                     env=env, stdout=log, stderr=log,
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                deadline = time.monotonic() + 15
                while not (data / 'library.sqlite').exists():
                    if first.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError('Startup process did not initialize its isolated library')
                    time.sleep(.05)
                time.sleep(.3)
                results = []
                for arguments in (['--startup'], []):
                    duplicate = subprocess.run([str(executable), *arguments, '--data', str(data)],
                                               env=env, stdout=log, stderr=log, timeout=10,
                                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                    assert duplicate.returncode == 0, 'Duplicate launch did not forward to the original'
                    assert first.poll() is None, 'Original application exited while handling a launch'
                    results.append({'arguments': arguments, 'exit_code': duplicate.returncode})
                return {'executable': str(executable), 'startup_remained_running': True,
                        'duplicate_launches': results, 'data_scope': 'temporary fixture only'}
            finally:
                # Only terminate the test process created above. Never discover or
                # close other BiliWave instances or touch their library directory.
                if first.poll() is None:
                    first.terminate()
                    first.wait(timeout=10)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--output')
    args = parser.parse_args()
    result = verify(args.executable)
    if args.output:
        Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=True))
