import importlib
import unittest

report = importlib.import_module('stem-report')


class ReportTest(unittest.TestCase):
    def test_union_conflicts_and_unsure(self):
        labels = [{'start': 0, 'end': 3, 'expected': 'dance'},
                  {'start': 1, 'end': 4, 'expected': 'dance'},
                  {'start': 2, 'end': 2.5, 'expected': 'no_dance'},
                  {'start': 0, 'end': 4, 'expected': 'unsure'}]
        parts = report.positive_parts(labels, 4)
        self.assertAlmostEqual(sum(b-a for a, b in parts), 3.5)
        self.assertFalse(any(a < 2.5 and b > 2 for a, b in parts))

    def test_power_averaging_and_partial_bins(self):
        # Two unequal powers: averaging dB instead of linear power is wrong.
        reference = {'source': {'frames': 3200}, 'levels': {
            'Mixture': [-200, -400], 'Drums': [-300, -500],
            'Bass': [-400, -600], 'Vocals': [-300, -500], 'Other': [-200, -400]}}
        levels = report.range_levels(reference, .05, .15)
        self.assertAlmostEqual(levels['relativeDb']['Drums'], -10)
        self.assertAlmostEqual(levels['mixtureDbFS'], -22.9670862, places=6)
        rows = [{'would_dance': 0} for _ in range(12)]
        reference['levels']['Drums'] = [-500, -500]
        missed = report.missed_profile(reference, rows, [(0, .192)])
        self.assertAlmostEqual(missed['missedSeconds'], .192)
        self.assertAlmostEqual(missed['missedWithDrumsBelowRelativeDb']['-20'], .1)


if __name__ == '__main__':
    unittest.main()
