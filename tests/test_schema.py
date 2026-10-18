import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"components"))
import odelic
import esphome.config_validation as cv

class CredentialValidation(unittest.TestCase):
    def test_leading_zeroes_are_preserved(self):
        self.assertEqual(odelic.lighting_id("00010007"),"00010007")
    def test_invalid_credentials(self):
        for value in [12345678,"1234567","123456789","1234abcd"," 12345678","１２３４５６７８"]:
            with self.assertRaises(cv.Invalid): odelic.lighting_id(value)

if __name__=="__main__": unittest.main()
