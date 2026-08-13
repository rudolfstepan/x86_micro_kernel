import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ISR_SOURCE = ROOT / "arch/x86/cpu/isr.asm"
REGISTERS_HEADER = ROOT / "arch/x86/include/sys.h"

# Intel-defined exceptions for which the processor puts an error code on the
# stack before entering the IDT handler.  A stub for one of these vectors must
# add only the vector number.  Every other stub has to synthesize a zero error
# code first so that all paths expose the same Registers layout to C.
CPU_ERROR_CODE_VECTORS = {8, 10, 11, 12, 13, 14, 17, 21, 29, 30}


def assembly_lines(source: str) -> list[str]:
    """Return normalized NASM instructions without comments or blank lines."""
    result = []
    for raw_line in source.splitlines():
        line = raw_line.split(";", 1)[0].strip().lower()
        if line:
            result.append(re.sub(r"\s+", " ", line))
    return result


def label_block(lines: list[str], label: str) -> list[str]:
    marker = f"{label.lower()}:"
    try:
        start = lines.index(marker) + 1
    except ValueError as error:
        raise AssertionError(f"missing assembly label {label}") from error

    end = start
    while end < len(lines) and not lines[end].endswith(":"):
        end += 1
    return lines[start:end]


def macro_block(lines: list[str], name: str) -> list[str]:
    marker = f"%macro {name.lower()} 1"
    try:
        start = lines.index(marker) + 1
    except ValueError as error:
        raise AssertionError(f"missing one-argument NASM macro {name}") from error

    try:
        end = lines.index("%endmacro", start)
    except ValueError as error:
        raise AssertionError(f"unterminated NASM macro {name}") from error
    return lines[start:end]


def macro_instructions(block: list[str]) -> list[str]:
    """Discard macro-generated labels/exports and return its instructions."""
    return [
        line
        for line in block
        if not line.startswith("global ") and not line.endswith(":")
    ]


def assert_pushes(test: unittest.TestCase, instruction: str, operand: str):
    test.assertRegex(
        instruction,
        rf"^push(?: byte| word| dword)? {re.escape(operand)}$",
    )


def register_fields(header: str) -> list[str]:
    match = re.search(
        r"typedef\s+struct\s*\{(?P<body>.*?)\}\s*Registers\s*;",
        header,
        flags=re.DOTALL,
    )
    if not match:
        raise AssertionError("Registers structure is missing")

    body = re.sub(r"//.*", "", match.group("body"))
    fields = []
    for declaration in body.split(";"):
        declaration = declaration.strip()
        if not declaration:
            continue
        typed_fields = re.fullmatch(r"uint32_t\s+(.+)", declaration)
        if not typed_fields:
            raise AssertionError(
                f"unexpected declaration in Registers: {declaration!r}"
            )
        fields.extend(field.strip() for field in typed_fields.group(1).split(","))
    return fields


class ExceptionStubSourceRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.isr_text = ISR_SOURCE.read_text(encoding="utf-8")
        cls.isr_lines = assembly_lines(cls.isr_text)
        cls.header_text = REGISTERS_HEADER.read_text(encoding="utf-8")

    def macro_invocations(self) -> dict[int, str]:
        invocations = {}
        pattern = re.compile(
            r"^(isr_no_error_code|isr_cpu_error_code) (0x[0-9a-f]+|[0-9]+)$"
        )
        for line in self.isr_lines:
            match = pattern.fullmatch(line)
            if not match:
                continue
            vector = int(match.group(2), 0)
            self.assertNotIn(vector, invocations,
                             f"duplicate ISR macro invocation for {vector}")
            invocations[vector] = match.group(1)
        return invocations

    def test_stub_macros_build_the_uniform_vector_error_pair(self):
        no_error_block = macro_block(self.isr_lines, "ISR_NO_ERROR_CODE")
        cpu_error_block = macro_block(self.isr_lines, "ISR_CPU_ERROR_CODE")
        for name, block in {
            "ISR_NO_ERROR_CODE": no_error_block,
            "ISR_CPU_ERROR_CODE": cpu_error_block,
        }.items():
            with self.subTest(macro=name):
                self.assertIn("global isr%1", block)
                self.assertIn("isr%1:", block)

        no_error = macro_instructions(no_error_block)
        cpu_error = macro_instructions(cpu_error_block)

        self.assertEqual(len(no_error), 4)
        self.assertEqual(no_error[0], "cli")
        assert_pushes(self, no_error[1], "0")
        assert_pushes(self, no_error[2], "%1")
        self.assertEqual(no_error[3], "jmp isr_common_stub")

        self.assertEqual(len(cpu_error), 3)
        self.assertEqual(cpu_error[0], "cli")
        assert_pushes(self, cpu_error[1], "%1")
        self.assertEqual(cpu_error[2], "jmp isr_common_stub")

    def test_every_vector_uses_exactly_the_correct_stub_macro(self):
        invocations = self.macro_invocations()
        self.assertEqual(set(invocations), set(range(32)))

        actual_cpu_error_vectors = {
            vector
            for vector, macro in invocations.items()
            if macro == "isr_cpu_error_code"
        }
        self.assertEqual(actual_cpu_error_vectors, CPU_ERROR_CODE_VECTORS)

    def test_alignment_check_uses_the_cpu_error_code_macro(self):
        # #AC (vector 17) is easy to misclassify: unlike the adjacent #MF and
        # #MC exceptions, the processor already pushed an error code.
        self.assertEqual(self.macro_invocations().get(17),
                         "isr_cpu_error_code")

    def test_registers_structure_matches_the_assembly_stack_order(self):
        fields = register_fields(self.header_text)
        expected = [
            "gs", "fs", "es", "ds",
            "edi", "esi", "ebp", "esp", "ebx", "edx", "ecx", "eax",
            "irq_number", "error_code",
            "eip", "cs", "eflags", "useresp", "ss",
        ]
        self.assertEqual(fields, expected)

        offsets = {field: index * 4 for index, field in enumerate(fields)}
        self.assertEqual(offsets["irq_number"], 48)
        self.assertEqual(offsets["error_code"], 52)
        self.assertEqual(offsets["eip"], 56)
        self.assertEqual(len(fields) * 4, 76)

    def test_registers_layout_is_guarded_by_compile_time_assertions(self):
        compact_header = re.sub(r"\s+", "", self.header_text)
        self.assertIn("#include<stddef.h>", compact_header)
        self.assertIn("_Static_assert(sizeof(Registers)==76,", compact_header)
        for field, offset in {
            "irq_number": 48,
            "error_code": 52,
            "eip": 56,
        }.items():
            with self.subTest(field=field):
                self.assertIn(
                    f"_Static_assert(offsetof(Registers,{field})=={offset},",
                    compact_header,
                )

    def test_common_stub_preserves_and_removes_exactly_one_uniform_frame(self):
        block = label_block(self.isr_lines, "isr_common_stub")
        required_sequence = [
            "pusha",
            "push ds", "push es", "push fs", "push gs",
            "mov ebx, esp",
            "and esp, 0xfffffff0", "sub esp, 12", "push ebx",
            "call exception_dispatcher",
            "mov esp, ebx",
            "pop gs", "pop fs", "pop es", "pop ds",
            "popa", "add esp, 8", "iret",
        ]

        position = -1
        for instruction in required_sequence:
            with self.subTest(instruction=instruction):
                position = block.index(instruction, position + 1)

        call = block.index("call exception_dispatcher")
        self.assertIn("cld", block[:call])
        self.assertEqual(block[-1], "iret")


if __name__ == "__main__":
    unittest.main()
