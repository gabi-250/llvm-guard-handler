import os


TEST_PROG_DIR = 'test_programs'


def get_test_dir(filename):
    return  os.path.join(os.path.dirname(os.path.realpath(filename)),
                         TEST_PROG_DIR)


def get_test_files(filename):
    test_dir = get_test_dir(filename)
    return [os.path.splitext(name)[0] for name in
                extract_files_with_extension(test_dir, '.c')]


def extract_files_with_extension(dir_name, extension):
    return [name for name in os.listdir(dir_name) if name.endswith(extension)]
