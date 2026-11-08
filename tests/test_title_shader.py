"""Check the assembled straight-line title shader's configured output writes."""
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def verify_output_writes(data):
    """Decode DVLE masks and PICA ALU destinations, not source-text patterns.

    This checker deliberately accepts only the title shader's straight-line
    MOV/MUL/MAD/DP4/END instructions; control flow needs a path-aware checker.
    """
    def word(offset):
        return struct.unpack_from("<I", data, offset)[0]

    if data[:4] != b"DVLB" or word(4) != 1:
        raise AssertionError("Expected one shader entry")
    dvle = word(8)
    dvlp = 12
    if data[dvlp:dvlp + 4] != b"DVLP" or data[dvle:dvle + 4] != b"DVLE":
        raise AssertionError("Invalid shader sections")
    code = dvlp + word(dvlp + 8)
    descriptors = dvlp + word(dvlp + 16)
    outputs = dvle + word(dvle + 40)
    configured = {}
    for index in range(word(dvle + 44)):
        _, register, mask = struct.unpack_from("<HHB", data, outputs + index * 8)
        configured[register] = configured.get(register, 0) | mask
    writes = {register: [0, 0, 0, 0] for register in configured}
    for index in range(word(dvle + 8), word(dvle + 12)):
        instruction = word(code + index * 4)
        opcode = instruction >> 26
        if opcode == 0x22:  # END
            break
        if opcode in (0x02, 0x08, 0x13):  # DP4, MUL, MOV
            register = (instruction >> 21) & 31
            descriptor = instruction & 127
        elif 0x30 <= opcode <= 0x3F:  # MADI/MAD have a distinct encoding
            register = (instruction >> 24) & 31
            descriptor = instruction & 31
        else:
            raise AssertionError(f"Unsupported instruction {opcode:#x}")
        if register not in configured:
            continue
        destination_mask = word(descriptors + descriptor * 8) & 15
        # DVLE uses X=bit0; PICA operand descriptors use X=bit3.
        for component in range(4):
            if destination_mask & (8 >> component):
                writes[register][component] += 1
    for register, mask in configured.items():
        for component in range(4):
            if mask & (1 << component) and writes[register][component] != 1:
                raise AssertionError(
                    f"o{register}.{'xyzw'[component]} configured but written "
                    f"{writes[register][component]} times")


class TitleShaderTests(unittest.TestCase):
    def test_compiled_output_components_are_written_once(self):
        assembler = shlex.split(os.environ.get("PICASSO", "picasso"))
        if not shutil.which(assembler[0]):
            self.skipTest("Picasso is required to verify compiled shader output masks")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "title.shbin"
            source = (ROOT / "platform/3ds/title_shader.v.pica").read_text()
            for broken in (False, True):
                with self.subTest(broken=broken):
                    shader = Path(directory) / "title.v.pica"
                    text = source
                    if broken:
                        text = text.replace("mov outTexCoord,", "mov outTexCoord.xy,")
                        text = text.replace("mov outTexCoord2,", "mov outTexCoord2.xy,")
                    shader.write_text(text)
                    result = subprocess.run(assembler + ["-o", str(output), str(shader)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    if broken:
                        with self.assertRaisesRegex(AssertionError, r"o2\.z.*0 times"):
                            verify_output_writes(output.read_bytes())
                    else:
                        verify_output_writes(output.read_bytes())


if __name__ == "__main__":
    unittest.main()
