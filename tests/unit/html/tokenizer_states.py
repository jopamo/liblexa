#!/usr/bin/env python3
"""Check that the private state definition covers the scalar dispatch."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]


class TokenizerStatesTest(unittest.TestCase):
    def test_dispatch_and_eof_coverage(self):
        definition = (ROOT / "src/html/tokenizer_states.def").read_text()
        definition = re.sub(r"/\*.*?\*/", "", definition, flags=re.S)
        entries = []
        for line in definition.splitlines():
            if not line.strip():
                continue
            match = re.fullmatch(
                r"LXA_HTML_STATE\(([A-Z_]+), ([A-Z_]+)\)", line)
            self.assertIsNotNone(match, line)
            entries.append(match.groups())
        names = [name for name, action in entries]
        self.assertTrue(names)
        self.assertEqual(len(names), len(set(names)))
        source = (ROOT / "src/html/tokenizer.c").read_text()
        dispatch = source.split("switch (tokenizer->state) {", 1)[1]
        cases = re.findall(r"case STATE_([A-Z_]+):", dispatch)
        self.assertCountEqual(names, cases)
        self.assertEqual(set(re.findall(r"\bSTATE_([A-Z_]+)\b", source)),
                         set(names))
        finish = source.split("lxa_html_tokenizer_finish(", 1)[1]
        self.assertEqual({action for name, action in entries},
                         set(re.findall(r"\bEOF_([A-Z_]+)\b", finish)))


if __name__ == "__main__":
    unittest.main()
