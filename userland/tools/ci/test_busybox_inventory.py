"""Source/config inventory must preserve disabled aliases and conditional variants."""
import tempfile
import unittest
from pathlib import Path
from busybox_inventory import declarations, selected_entries


class AppletInventoryTests(unittest.TestCase):
    def test_preprocessor_selects_real_variants_and_feature_guards(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'include').mkdir()
            (root / 'shell').mkdir()
            (root / 'networking').mkdir()
            (root / 'shell/ash.c').write_text(
                '//applet:IF_ASH(APPLET(ash, BB_DIR_BIN, BB_SUID_DROP))\n'
                '//applet:IF_SH_IS_ASH(APPLET_ODDNAME(sh, ash, BB_DIR_BIN, BB_SUID_DROP, ash))\n')
            (root / 'shell/hush.c').write_text(
                '//applet:IF_SH_IS_HUSH(APPLET_ODDNAME(sh, hush, BB_DIR_BIN, BB_SUID_DROP, hush))\n')
            (root / 'networking/tftp.c').write_text(
                '//applet:#if ENABLE_FEATURE_TFTP_GET || ENABLE_FEATURE_TFTP_PUT\n'
                '//applet:IF_TFTP(APPLET(tftp, BB_DIR_USR_BIN, BB_SUID_DROP))\n'
                '//applet:#endif\n')
            (root / 'include/autoconf.h').write_text(
                '#define IF_ASH(...) __VA_ARGS__\n'
                '#define IF_SH_IS_ASH(...) __VA_ARGS__\n'
                '#define IF_SH_IS_HUSH(...)\n'
                '#define IF_TFTP(...) __VA_ARGS__\n'
                '#define ENABLE_FEATURE_TFTP_GET 0\n'
                '#define ENABLE_FEATURE_TFTP_PUT 0\n')
            rows, fragments = declarations(root)
            self.assertEqual(len(rows), 4)
            self.assertEqual(selected_entries(root, fragments),
                             {('ash', 'ash', '/bin'), ('sh', 'ash', '/bin')})
            self.assertEqual({r['main'] for r in rows if r['name'] == 'sh'},
                             {'ash', 'hush'})

    def test_unknown_declaration_fails_instead_of_silent_omission(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'coreutils').mkdir()
            (root / 'coreutils/new.c').write_text('//applet:NEW_APPLET(foo)\n')
            with self.assertRaisesRegex(ValueError, 'Unaccounted'):
                declarations(root)

    def test_non_c_identifier_names_and_comment_are_accounted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'coreutils').mkdir()
            (root / 'coreutils/test.c').write_text(
                '//applet:/* explanatory comment */\n'
                '//applet:IF_TEST1(APPLET_NOFORK([,test,BB_DIR_USR_BIN,BB_SUID_DROP,test))\n'
                '//applet:IF_X(APPLET_ODDNAME(run-parts,run_parts,BB_DIR_BIN,BB_SUID_DROP,x))\n')
            rows, _ = declarations(root)
            self.assertEqual([r['name'] for r in rows], ['[', 'run-parts'])


if __name__ == '__main__':
    unittest.main()
