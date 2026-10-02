#!/usr/bin/env python3
"""Regression tests for libfsp's parser helper and Flex post-processor."""

import pathlib
import os
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HELPER = ROOT / "scripts" / "fsp-helper.py"
POSTPROCESS_FLEX = ROOT / "scripts" / "postprocess-flex.py"
LEXER = ROOT / "test_lexer.l"


class ScriptRegressionTests(unittest.TestCase):
    def run_command(self, command, **kwargs):
        cwd = kwargs.pop("cwd", ROOT)
        kwargs.setdefault("text", True)
        kwargs.setdefault("stdout", subprocess.PIPE)
        kwargs.setdefault("stderr", subprocess.PIPE)
        return subprocess.run(
            command,
            cwd=cwd,
            check=False,
            **kwargs,
        )

    def test_multiline_user_action_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            lexer = pathlib.Path(directory) / "valid.l"
            lexer.write_text(
                "%option reentrant bison-bridge\n"
                "%{\n"
                "#define YY_INPUT(buf,result,max_size) \\\n"
                " result = fsp_read_input(yyextra, buf, max_size)\n"
                "#define YY_USER_ACTION \\\n"
                " FSP_LEXER_USER_ACTION(yyextra)\n"
                "%}\n%%\n%%\n",
                encoding="utf-8",
            )
            parser = pathlib.Path(directory) / "valid.y"
            parser.write_text(
                "%define api.push-pull push\n%define api.pure full\n",
                encoding="utf-8",
            )

            result = self.run_command(
                [
                    "python3",
                    str(HELPER),
                    "validate",
                    "--lexer",
                    str(lexer),
                    "--parser",
                    str(parser),
                    "--strict",
                ]
            )
            self.assertEqual(
                result.returncode, 0, (result.stdout or "") + result.stderr
            )

            lexer.write_text(
                lexer.read_text(encoding="utf-8").replace(
                    " FSP_LEXER_USER_ACTION(yyextra)\n", " return 0;\n"
                ),
                encoding="utf-8",
            )
            result = self.run_command(
                [
                    "python3",
                    str(HELPER),
                    "validate",
                    "--lexer",
                    str(lexer),
                    "--strict",
                ]
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("YY_USER_ACTION", result.stderr)

    def test_generated_parser_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "generated.c"
            result = self.run_command(
                [
                    "python3",
                    str(HELPER),
                    "generate",
                    "--lexer-prefix",
                    "test_lexer",
                    "--parser-prefix",
                    "test_parser",
                    "--function-name",
                    "test_generated_parse",
                    "--output",
                    str(output),
                ]
            )
            self.assertEqual(
                result.returncode, 0, (result.stdout or "") + result.stderr
            )
            generated = output.read_text(encoding="utf-8")
            for expected in (
                "fsp_parse_chunk(ctx",
                "test_lexer_fsp_commit(scanner)",
                "test_lexer_fsp_rewind(scanner)",
                "TEST_PARSER_DISCARD_LVAL(&lval)",
                "test_parser_pstate_delete(pstate)",
            ):
                self.assertIn(expected, generated)

    def test_flex_postprocessor_emits_hooks(self):
        with tempfile.TemporaryDirectory() as directory:
            generated_dir = pathlib.Path(directory)
            result = self.run_command(
                ["flex", "-o", "raw.c", str(LEXER)], cwd=generated_dir
            )
            self.assertEqual(
                result.returncode, 0, (result.stdout or "") + result.stderr
            )

            output = generated_dir / "processed.c"
            with output.open("w", encoding="utf-8") as stream:
                result = self.run_command(
                    [
                        "python3",
                        str(POSTPROCESS_FLEX),
                        "--fsp-rewind",
                        "raw.c",
                    ],
                    cwd=generated_dir,
                    stdout=stream,
                )
            self.assertEqual(
                result.returncode, 0, (result.stdout or "") + result.stderr
            )
            generated = output.read_text(encoding="utf-8")
            self.assertIn("yycleanup(yyscanner);", generated)
            self.assertIn(
                "test_lexer_fsp_commit(yyscan_t yyscanner)", generated
            )
            self.assertIn(
                "test_lexer_fsp_rewind(yyscan_t yyscanner)", generated
            )

    def test_reject_rule_falls_through_to_next_match(self):
        with tempfile.TemporaryDirectory() as directory:
            generated_dir = pathlib.Path(directory)
            lexer = generated_dir / "reject.l"
            lexer.write_text(
                "%option noyywrap\n%%\n"
                '"reject"[0-9]+ { REJECT; }\n'
                "[A-Za-z][A-Za-z0-9]* { printf(\"ID:%s\\n\", yytext); }\n"
                ".|\\n ;\n%%\n"
                "int main(void) { return yylex(); }\n",
                encoding="utf-8",
            )
            result = self.run_command(
                ["flex", "-o", "reject.c", str(lexer)], cwd=generated_dir
            )
            self.assertEqual(
                result.returncode, 0, (result.stdout or "") + result.stderr
            )
            result = self.run_command(
                [os.environ.get("CC", "cc"), "reject.c", "-o", "reject"],
                cwd=generated_dir,
            )
            self.assertEqual(
                result.returncode, 0, (result.stdout or "") + result.stderr
            )
            result = self.run_command(
                [str(generated_dir / "reject")],
                cwd=generated_dir,
                input="reject7",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "ID:reject7\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
