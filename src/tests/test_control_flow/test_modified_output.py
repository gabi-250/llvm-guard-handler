import os
import argparse
import subprocess
import pytest
from .. import support


def setup_module(module):
    test_dir = support.get_test_dir(__file__)
    subprocess.run('cd {:} && make clean && make'.format(test_dir), shell=True)


@pytest.mark.parametrize('name', support.get_test_files(__file__))
def test_output(name):
    test_dir = support.get_test_dir(__file__)
    bin_path = '{test_dir}/{name}'.format(test_dir=test_dir, name=name)
    p = subprocess.run(bin_path, shell=True, stdout=subprocess.PIPE)
    if p.returncode:
        pytest.skip('Could not compile {:}.c.'.format(name))

    out_file = '{:}.out'.format(bin_path)
    try:
        with open(out_file) as f:
            expected_output = f.read()
        output = p.stdout.decode()
        assert output == expected_output
    except FileNotFoundError:
        pytest.skip('Could not find {:}.out.'.format(name))


def teardown_module(module):
    test_dir = support.get_test_dir(__file__)
    subprocess.run('cd {:} && make clean'.format(test_dir), shell=True)
