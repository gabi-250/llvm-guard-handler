import os
import argparse
import subprocess
import pytest
from . import support


def setup_module(module):
    test_dir = support.get_test_dir(__file__)
    subprocess.run('cd {:} && make clean && make'.format(test_dir), shell=True)


@pytest.mark.parametrize('name', support.get_test_files(__file__))
def test_output(name):
    test_dir = support.get_test_dir(__file__)
    bin_path = '{test_dir}/{name}'.format(test_dir=test_dir, name=name)
    p = subprocess.run(bin_path, shell=True, stdout=subprocess.PIPE)
    clang_bin = '{path}_clang_'.format(path=bin_path)
    clang_compile = 'clang -o {clang_bin} {path}.c'.format(clang_bin=clang_bin,
                                                           path=bin_path)
    compile_proc = subprocess.run(clang_compile, shell=True,
                                  stdout=subprocess.PIPE)
    if compile_proc.returncode or p.returncode:
        pytest.skip('Could not compile the test file.')
    compile_proc = subprocess.run('{:}'.format(clang_bin), shell=True,
                                  stdout=subprocess.PIPE)
    expected_output = compile_proc.stdout.decode()
    output = p.stdout.decode()
    assert output == expected_output


def teardown_module(module):
    test_dir = support.get_test_dir(__file__)
    subprocess.run('cd {:} && make clean'.format(test_dir), shell=True)
