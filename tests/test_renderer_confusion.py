"""Execute production type12 GPU resource and composite contracts on host."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ConfusionRendererTests(unittest.TestCase):
    def test_resource_rollback_composite_and_segment_budget(self):
        cc=shlex.split(os.environ.get("CC","cc"))
        if not shutil.which(cc[0]):self.skipTest("Host C compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/"confusion"
            result=subprocess.run(cc+["-std=c11","-ffunction-sections","-Wl,--gc-sections",
                "-O2","-Wall","-Wextra","-Wpedantic","-Werror",
                "-I"+str(ROOT/"include"),"-I"+str(ROOT/"platform/3ds"),
                str(ROOT/"tests/renderer_confusion.c"),str(ROOT/"src/confusion_indirect.c"),
                str(ROOT/"src/wzx_confusion.c"),"-lm","-o",str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
