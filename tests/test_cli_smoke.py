import unittest

from onec_ordinary_forms import __version__
from onec_ordinary_forms.cli import replace_root_title


class CliSmokeTest(unittest.TestCase):
    def test_version_is_present(self) -> None:
        self.assertRegex(__version__, r"^\d+\.\d+\.\d+$")

    def test_replace_root_title_updates_first_title_only(self) -> None:
        source = '{"ru","Old"}\n{"ru","Other"}\n'
        result = replace_root_title(source, "New")
        self.assertEqual(result, '{"ru","New"}\n{"ru","Other"}\n')


if __name__ == "__main__":
    unittest.main()
