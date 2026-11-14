"""Missed-result recovery, including guards against false and duplicate records."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from sf6tracker.config import Config
from sf6tracker.detector import Detector, Library
from sf6tracker.store import Store


def observed(side='p1', rating=1500, char='ryu', opp='ken', label='MR', votes=2):
    m = Detector._new_match(1000)
    other = 'p2' if side == 'p1' else 'p1'
    m['side'][side] = votes
    m['you'] = m['named'] = votes
    m['hud_seen'] = 1000
    m['chars'][side][char] = votes
    m['chars'][other][opp] = votes
    m['rating'][side][(rating, label)] = votes
    return m


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        cfg, self.store = Config(self.tmp.name), Store(self.tmp.name)
        self.det = Detector(cfg, self.store, Library(self.tmp.name), ocr=object())
        self.store.add(ts=900, result='win', my_char='ryu', opp_char='ken', rating=1500, rating_label='MR')
        self.det.m = observed()
        self.det._sf6_arm_recovery()

    def read(self, rating=1508, **kwargs):
        return self.det._sf6_recover_rating(observed(rating=rating, **kwargs), 1040)

    def test_missed_win_keeps_previous_opponent_after_side_swap(self):
        self.assertIsNone(self.read(side='p2', opp='juri'))
        self.assertIsNone(self.read(side='p2', opp='juri'))
        rec = self.read(side='p2', opp='juri')
        self.assertEqual((rec['result'], rec['rating_before'], rec['rating'], rec['delta']), ('win', 1500, 1508, 8))
        self.assertEqual((rec['side'], rec['my_char'], rec['opp_char']), ('p1', 'ryu', 'ken'))
        self.assertIn('補完', rec['note'])
        for _ in range(5): self.assertIsNone(self.read())
        self.assertEqual(len(self.store.matches()), 2)

    def test_missed_loss(self):
        for _ in range(3): rec = self.read(1492)
        self.assertEqual((rec['result'], rec['delta']), ('lose', -8))

    def test_unchanged_and_nonconsecutive_readings_do_not_record(self):
        for value in (1500, 1508, 1500, 1508, 1509, 1508):
            self.assertIsNone(self.read(value))
        self.assertEqual(len(self.store.matches()), 1)

    def test_outlier_does_not_record(self):
        for _ in range(4): self.assertIsNone(self.read(9999))
        self.assertEqual(len(self.store.matches()), 1)

    def test_character_change_does_not_record(self):
        for _ in range(4): self.assertIsNone(self.read(char='juri'))
        self.assertIsNone(self.det.recovery)

    def test_lp_mr_change_does_not_record(self):
        for _ in range(4): self.assertIsNone(self.read(label='LP'))
        self.assertIsNone(self.det.recovery)

    def test_new_session_does_not_record(self):
        self.store.new_session()
        for _ in range(4): self.assertIsNone(self.read())
        self.assertEqual(len(self.store.matches()), 1)

    def test_manual_record_cancels_recovery(self):
        self.store.add(ts=1030, result='win', my_char='ryu', rating=1508, rating_label='MR')
        for _ in range(4): self.assertIsNone(self.read())
        self.assertEqual(len(self.store.matches()), 2)

    def test_already_saved_rating_is_not_recorded_again(self):
        self.store.update(self.store.last()['id'], rating=1508)
        for _ in range(4): self.assertIsNone(self.read())
        self.assertEqual(len(self.store.matches()), 1)

    def test_normal_result_only_settles_existing_record(self):
        self.det._sf6_record('win', 1020)
        self.assertIsNone(self.det.recovery)
        for _ in range(4): self.assertIsNone(self.read())
        self.det._sf6_settle_previous((1508, 'MR'), 'ryu', 1040)
        self.assertEqual(len(self.store.matches()), 2)
        self.assertEqual(self.store.last()['delta'], 8)

    def test_no_observed_battle_does_not_recover(self):
        self.det.recovery = None
        self.det.m = observed(votes=1)
        self.det._sf6_arm_recovery()
        for _ in range(4): self.assertIsNone(self.read())

    def test_lp_win(self):
        self.det.recovery = None
        self.det.m = observed(rating=18000, label='LP')
        self.det._sf6_arm_recovery()
        for _ in range(3): rec = self.read(18050, label='LP')
        self.assertEqual((rec['result'], rec['delta'], rec['rating_label']), ('win', 50, 'LP'))

    def test_unchanged_rating_follows_new_opponent_without_inventing_a_result(self):
        self.det.m = observed(side='p2', opp='juri')
        self.det._sf6_arm_recovery()
        for _ in range(3): rec = self.read(side='p2', opp='juri')
        self.assertEqual((rec['side'], rec['opp_char']), ('p2', 'juri'))
        self.assertEqual(len(self.store.matches()), 2)


if __name__ == '__main__':
    unittest.main()
