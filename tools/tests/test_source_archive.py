"""Verify the documented build entry point outside any Git checkout."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest


class SourceArchiveTest(unittest.TestCase):
    def test_build_without_git_uses_revision_marker(self):
        with tempfile.TemporaryDirectory(prefix='cp-source-') as directory:
            root = Path(directory)
            (root / 'tools').mkdir()
            shutil.copy2(Path(__file__).parents[1] / 'verify.py', root / 'tools/verify.py')
            (root / 'SOURCE_REVISION').write_text('archive-test-revision\n')
            project = root / 'src/userclient'
            project.mkdir(parents=True)
            (project / 'userclient.pro').write_text(
                'QT -= gui\nQT += core\nCONFIG += console c++17\nTARGET = UserClient\n'
                'TEMPLATE = app\nSOURCES += main.cpp\n')
            (project / 'main.cpp').write_text('#include <QtGlobal>\nint main(){return qVersion()[0] == \'6\' ? 0 : 1;}\n')
            result = subprocess.run([sys.executable, 'tools/verify.py', '--filter', 'userclient.pro'],
                                    cwd=root, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((root / 'build-verification/report-userclient.pro.json').read_text())
            self.assertEqual(report['commit'], 'archive-test-revision')
            self.assertFalse(report['dirty'])
            self.assertTrue((root / 'build-verification/src__userclient__userclient/UserClient').is_file())


if __name__ == '__main__':
    unittest.main()
