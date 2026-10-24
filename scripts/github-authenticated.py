"""Run GitHub CLI using Git's credential helper, without saving or printing tokens."""
import argparse
import os
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--gh', default=shutil.which('gh'))
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.gh:
        parser.error('Install GitHub CLI or supply --gh with its path.')
    env = dict(os.environ, GIT_TERMINAL_PROMPT='0', GCM_INTERACTIVE='never', GH_PROMPT_DISABLED='1')
    if not (env.get('GH_TOKEN') or env.get('GITHUB_TOKEN')):
        credential = subprocess.run(
            ['git', 'credential', 'fill'], input='protocol=https\nhost=github.com\n\n',
            capture_output=True, text=True, env=env, timeout=30)
        values = dict(line.split('=', 1) for line in credential.stdout.splitlines() if '=' in line)
        if credential.returncode or not values.get('password'):
            print('GitHub authentication unavailable. Sign in using Git credential manager or gh auth login.', file=sys.stderr)
            return 2
        env['GH_TOKEN'] = values['password']
    commands = args.arguments[1:] if args.arguments[:1] == ['--'] else args.arguments
    return subprocess.run([args.gh, *commands], env=env).returncode


if __name__ == '__main__':
    sys.exit(main())
