import os
import argparse
import subprocess
from colorama import Fore, Style


def extract_files_with_extension(dir_name, extension):
    return [name for name in os.listdir(dir_name) if name.endswith(extension)]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Run the integration tests.')
    parser.add_argument('-d', '--test-dir', type=str,
                        help='The test directory.', required=True)
    args = parser.parse_args()
    subprocess.run('cd {:} && make clean && make'.format(args.test_dir),
                   shell=True, stdout=subprocess.PIPE)
    c_files = extract_files_with_extension(args.test_dir, '.c')
    bin_names = [os.path.splitext(name)[0] for name in c_files]
    failed = []
    skipped = []
    for i, name in enumerate(bin_names):
        print('Running test {:}: {:}\n'.format(i, name))
        command = '{tests_dir}/{name}'.format(tests_dir=args.test_dir,
                                              name=name)
        p = subprocess.run(command, shell=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
        out_file = '{tests_dir}/{out_file}'.format(tests_dir=args.test_dir,
                                                   out_file=name + '.out')
        try:
            with open(out_file) as f:
                content = f.read()
            output = p.stdout.decode()
            if content.strip() == output.strip():
                print(Fore.GREEN + 'Passed!')
            else:
                print(Fore.RED + 'Failed!')
                print('Expected:\n{:}\nFound:\n{:}\n'.format(content, output))
                failed.append(name)
            print(Style.RESET_ALL)
        except FileNotFoundError:
            print('Output file not found. Skipping...')
            skipped.append(name)
        print('=' * 30)
    if not failed:
        print(Fore.GREEN  + 'Success: all tests passed.\n')
    else:
        print(Fore.RED + '{}/{} tests passed'.format(len(bin_names) - len(failed),
                                                     len(bin_names)))
    if skipped:
        print(Fore.YELLOW + '{}/{} tests skipped'.format(len(skipped),
                                                         len(bin_names)))
