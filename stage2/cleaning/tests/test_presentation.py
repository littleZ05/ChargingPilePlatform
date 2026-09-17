import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('presentation', Path(__file__).parents[1]/'build_presentation.py')
presentation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(presentation)


class PresentationTest(unittest.TestCase):
    def test_modified_inputs_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)
            (source/'broken.csv').write_text('modified')
            (source/'manifest.json').write_text(json.dumps({'output_sha256':{'broken.csv':'wrong'}}))
            with self.assertRaisesRegex(ValueError,'modified'):
                presentation.build(source,source/'index.html')
            self.assertFalse((source/'index.html').exists())


if __name__=='__main__':
    unittest.main()
