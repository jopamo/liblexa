#!/usr/bin/env python3
"""Check private state IDs, transition destinations and EOF action coverage."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[3]


class TokenizerStatesTest(unittest.TestCase):
    def test_dispatch_and_eof_coverage(self):
        definition = (ROOT / "src/html/tokenizer_states.def").read_text()
        entries = re.findall(
            r"^LXA_HTML_STATE\(([A-Z_]+), (\d+), ([A-Z_]+),",
            definition, flags=re.M)
        names = [name for name, state_id, action in entries]
        self.assertTrue(names)
        self.assertEqual(len(names), len(set(names)))
        self.assertCountEqual([int(state_id) for name, state_id, action in entries],
                              range(len(entries)))
        source = (ROOT / "src/html/tokenizer.c").read_text()
        self.assertEqual(source.count('#include "html/tokenizer_states.def"'), 3)
        self.assertNotRegex(source, r"case STATE_[A-Z_]+:")
        self.assertEqual(set(re.findall(r"\bSTATE_([A-Z_]+)\b",
                                       source + definition)),
                         set(names))
        finish = source.split("lxa_html_tokenizer_finish(", 1)[1]
        self.assertEqual({action for name, state_id, action in entries},
                         set(re.findall(r"\bEOF_([A-Z_]+)\b", finish)))


if __name__ == "__main__":
    unittest.main()
