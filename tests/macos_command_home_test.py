#!/usr/bin/env python3
"""Offline installed-executable regression for the default macOS directory layout."""
import argparse
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ava', required=True)
    args = parser.parse_args()
    args.ava = str(pathlib.Path(args.ava).absolute())
    with tempfile.TemporaryDirectory(prefix='ava-home-layout-') as temporary:
        root = pathlib.Path(temporary).resolve()
        home = root / 'home'
        workspace = home / 'src' / 'repo'
        workspace.mkdir(parents=True, mode=0o700)
        # No XDG overrides: AVA must use HOME/.local/state/ava, as installed.
        # A hostile TMPDIR inside HOME must not choose the command environment.
        bad_tmp = home / 'bad-tmp'
        bad_tmp.mkdir(mode=0o700)
        env = {'HOME': str(home), 'PATH': '/usr/bin:/bin:/usr/sbin:/sbin',
               'TMPDIR': str(bad_tmp), 'TERM': 'dumb', 'NO_COLOR': '1'}
        subprocess.run(['/usr/bin/git', 'init', '-q', str(workspace)], env=env, check=True)
        commands = '/bash /bin/echo AVA-MAC-SHELL-OK\nallow\n/bash git status\nallow\n'
        commands += '/bash /bin/echo MUST-NOT-RUN\ndeny\n'
        commands += '/bash /usr/bin/env\nallow\n/exit\n'
        result = subprocess.run([args.ava, '--line-shell', '--offline'], input=commands,
                                cwd=workspace, env=env, text=True, capture_output=True, timeout=30)
        output = result.stdout + result.stderr
        assert result.returncode == 0, output
        assert 'must be disjoint' not in output, output
        assert output.count('Permission required') == 4, output
        assert 'Risk: critical' in output and 'Allow once' in output, output
        assert '\nAVA-MAC-SHELL-OK\n' in output, output
        assert 'On branch' in output, output
        assert '\nMUST-NOT-RUN\n' not in output, output
        values = {}
        for line in output.splitlines():
            key, sep, value = line.partition('=')
            if sep and key in {'HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_DATA_HOME','XDG_STATE_HOME','TMPDIR'}:
                values[key] = pathlib.Path(value)
        assert len(values) == 6, output
        parents = {p.parent for p in values.values()}
        assert len(parents) == 1, values
        command_root = parents.pop()
        assert not command_root.is_relative_to(home), values
        assert not command_root.exists(), 'command environment leaked after completion'
        assert not list(bad_tmp.iterdir()), 'command environment trusted TMPDIR'
        assert (home / '.local/state/ava/sessions').is_dir()
        print(json.dumps({'default_home_layout': 'passed', 'echo': 'approved', 'git_status': 'approved',
                          'denial': 'passed', 'private_environment_cleanup': 'passed', 'TMPDIR_ignored': True}))


if __name__ == '__main__':
    main()
