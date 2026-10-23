"""Test the production cleanup policy and partition integration without a device.

The policy driver includes the real production header and tests real POSIX node
probes. The supplementary dispatch harness compiles actual partition guards and
dispatch, stopping at fake formatters; it does not validate a kernel dm ioctl or
claim to format a filesystem. No dmctl/dmsetup command is executed by these tests.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent
TOOLS = (
    ('none', [], 'none'),
    ('of-dmctl', ['-DOF_USE_DMCTL=1'], 'dmctl delete userdata'),
    ('tw-dmctl', ['-DTW_USE_DMCTL=1'], 'dmctl delete userdata'),
    ('dmsetup', ['-DAERA_USE_DMSETUP=1'], 'dmsetup remove -f userdata'),
)


def definition(source, signature):
    begin = source.index(signature)
    opening = source.index('{', begin)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


def compile_driver(path, binary, flags):
    result = subprocess.run(['g++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra',
                             '-Werror', *flags, str(path), '-o', str(binary)],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'),
                     'Requires Linux and a native C++ compiler')
class UserdataMappingPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.binaries = []
        for name, flags, command in TOOLS:
            binary = Path(cls.temp.name) / name
            compile_driver(TESTS / 'userdata_mapping_driver.cpp', binary, flags)
            cls.binaries.append((name, binary, command))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_scenarios(self, *scenarios):
        for name, binary, _ in self.binaries:
            for scenario in scenarios:
                with self.subTest(tool=name, scenario=scenario):
                    subprocess.run([str(binary), scenario], check=True, timeout=5,
                                   capture_output=True, text=True)

    def test_existing_flags_select_the_original_commands(self):
        for name, binary, expected in self.binaries:
            with self.subTest(tool=name):
                result = subprocess.run([str(binary), 'command'], check=True,
                                        capture_output=True, text=True)
                self.assertEqual(result.stdout.strip(), expected)

    def test_real_probes_distinguish_absence_dangling_links_and_errors(self):
        self.check_scenarios('probe')

    def test_absent_mapping_does_not_execute_or_wait(self):
        self.check_scenarios('absent')

    def test_success_waits_only_as_long_as_removal_needs(self):
        self.check_scenarios('immediate', 'delayed')

    def test_final_deadline_check_and_surviving_mapping(self):
        self.check_scenarios('deadline', 'too-late', 'survives')

    def test_failed_or_missing_tool_stops_cleanup(self):
        self.check_scenarios('command-failed', 'tool-missing')

    def test_initial_and_post_command_probe_errors_stop_cleanup(self):
        self.check_scenarios('initial-error', 'poll-error')


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('g++'),
                     'Requires Linux and a native C++ compiler')
class UserdataMappingDispatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        source = (ROOT / 'partition.cpp').read_text()
        helper = definition(source, 'static bool Release_Userdata_Mapping(')
        # Redirect only the mapper node to the fixture's regular temporary file.
        # Keep the real lstat probe, policy and command adapter; Exec_Cmd and
        # sleeps are implemented by the host scaffold, never by a shell.
        assert helper.count('"/dev/block/mapper/userdata"') == 1
        helper = helper.replace('"/dev/block/mapper/userdata"', 'mapping_path.c_str()')
        sections = [helper, definition(source, 'bool TWPartition::Wipe(string'),
                    definition(source, 'bool TWPartition::Format_File_System('),
                    definition(source, 'bool TWPartition::Wipe_Encryption(')]
        for signature, result in (('bool TWPartition::Wipe_EXTFS(', 'File_System'),
                                  ('bool TWPartition::Wipe_EXT4()', '"ext4"'),
                                  ('bool TWPartition::Wipe_F2FS()', '"f2fs"')):
            body = definition(source, signature)
            # Preserve each actual unmount/target/cleanup prefix. The first
            # device-size query and all real formatter work are out of scope.
            body = body[:body.index('\tunsigned long long dev_sz =')]
            if 'Wipe_F2FS' in signature:
                body += '\t(void)f2fs_command; (void)needs_casefold;\n'
            else:
                body += '\t(void)sizeof(ret);\n'
            body += '\t(void)NeedPreserveFooter; return RecordFormat(' + result + ');\n'
            if 'Wipe_EXT4' in signature:
                body += '#else\n\treturn Wipe_EXTFS("ext4");\n#endif\n'
            sections.append(body + '}\n')
        scaffold = (TESTS / 'userdata_mapping_dispatch_driver.cpp').read_text()
        scaffold = scaffold.replace('#include "../../aera_userdata_mapping.hpp"',
                                    '#include "' + str(ROOT / 'aera_userdata_mapping.hpp') + '"')
        path = directory / 'dispatch.cpp'
        path.write_text(scaffold.replace('// PARTITION_FUNCTIONS', '\n'.join(sections)))
        cls.binaries = []
        for name, tool_flags, _ in TOOLS:
            for crypto in (False, True):
                for legacy_ext4 in (False, True):
                    variant = f'{name}-crypto-{int(crypto)}-legacy-ext4-{int(legacy_ext4)}'
                    binary = directory / variant
                    flags = tool_flags + (['-DTW_INCLUDE_CRYPTO'] if crypto else [])
                    flags += ['-DUSE_EXT4'] if legacy_ext4 else []
                    # Upstream Wipe() retains an unused recreate_media local.
                    compile_driver(path, binary, flags + ['-Wno-unused-but-set-variable'])
                    cls.binaries.append((variant, binary))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_scenario(self, scenario):
        for variant, binary in self.binaries:
            with self.subTest(variant=variant):
                subprocess.run([str(binary), scenario], check=True, timeout=5,
                               capture_output=True, text=True)

    def test_encryption_removal_checks_cleanup_before_state_and_key_changes(self):
        self.check_scenario('encryption')

    def test_direct_ext2_ext3_ext4_and_f2fs_paths_check_cleanup(self):
        self.check_scenario('direct')

    def test_intended_decrypted_formatter_target_is_preserved(self):
        self.check_scenario('preserve')

    def test_media_preserving_wipe_does_not_release_mapping(self):
        self.check_scenario('media')

    def test_other_partition_cannot_release_userdata(self):
        self.check_scenario('other')

    def test_unmount_failure_cannot_remove_or_format(self):
        self.check_scenario('unmount')

    def test_unsupported_filesystem_does_not_release_mapping(self):
        self.check_scenario('unknown')


if __name__ == '__main__':
    unittest.main()
