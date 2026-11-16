"""Retention and persistence checks, shared by Python and the compiled C++ store.

python tests/test_store.py [path-to-store_check]
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from sf6tracker.store import Store

NATIVE = sys.argv.pop(1) if len(sys.argv) > 1 else None


class NativeStore:
    def __init__(self, folder):
        self.proc = subprocess.Popen([NATIVE, folder], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, encoding='utf-8')
        assert json.loads(self.proc.stdout.readline()) == {'ready': True}

    def call(self, op, **args):
        self.proc.stdin.write(json.dumps(dict(op=op, **args)) + '\n')
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError(self.proc.stderr.read())
        return json.loads(line)

    def close(self):
        _, err = self.proc.communicate(timeout=5)
        if self.proc.returncode:
            raise RuntimeError(err)

    def add(self, **fields): return self.call('add', fields=fields)
    def update(self, match_id, **fields): return self.call('update', id=match_id, fields=fields)
    def get(self, match_id): return self.call('get', id=match_id)
    def matches(self): return self.call('matches')
    def last(self): return self.call('last')
    def new_session(self): return self.call('session')
    def observe_character(self, char): return self.call('character', character=char)
    def set_retention_hours(self, hours, now=None): return self.call('retention', hours=hours, now=time.time() if now is None else now)
    def prune(self, now=None): return self.call('prune', now=time.time() if now is None else now)


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = os.path.join(self.tmp.name, 'history.json')
        self.store = NativeStore(self.tmp.name) if NATIVE else Store(self.tmp.name)
        if NATIVE: self.addCleanup(self.store.close)

    def reload(self):
        if NATIVE: self.store.call('reload')
        else: self.store = Store(self.tmp.name, self.store.retention_hours)

    def saved(self):
        with open(self.path, encoding='utf-8') as stream:
            return json.load(stream)

    def test_count_is_unlimited_and_survives_reload(self):
        for i in range(1, 27): self.store.add(ts=i, result='win')
        self.assertEqual([m['id'] for m in self.store.matches()], list(range(1, 27)))
        self.assertIsNotNone(self.store.get(1))
        self.assertEqual(len(self.saved()['matches']), 26)
        self.assertEqual(self.saved()['next_id'], 27)
        self.reload()
        self.assertEqual([m['id'] for m in self.store.matches()], list(range(1, 27)))

    def test_existing_file_is_not_trimmed_by_count(self):
        data = {'version': 1, 'session': {'id': 7, 'started': 1}, 'next_id': 41,
                'matches': [{'id': i, 'ts': i, 'session': 7, 'result': 'win'} for i in range(1, 41)]}
        with open(self.path, 'w', encoding='utf-8') as stream: json.dump(data, stream)
        self.reload()
        self.assertEqual([m['id'] for m in self.store.matches()], list(range(1, 41)))
        self.assertEqual(len(self.saved()['matches']), 40)
        self.assertEqual(self.saved()['session']['id'], 7)
        self.assertEqual(self.saved()['next_id'], 41)
        self.assertEqual(os.listdir(self.tmp.name), ['history.json'])

    def test_backdated_addition_does_not_evict_a_newer_match(self):
        for i in range(1, 26): self.store.add(ts=1000+i)
        self.store.add(ts=0)
        self.assertEqual(len(self.store.matches()), 26)
        self.assertEqual(self.store.last()['id'], 25)

    def test_same_timestamp_uses_id_without_count_limit(self):
        for _ in range(30): self.store.add(ts=100)
        self.assertEqual([m['id'] for m in self.store.matches()], list(range(1, 31)))

    def test_updates_remain_attached_to_the_correct_match_after_sort(self):
        for i in range(1, 26): self.store.add(ts=i)
        updated = self.store.update(1, ts=100, result='lose')
        self.assertEqual((updated['id'], updated['result']), (1, 'lose'))
        self.assertEqual(self.store.last()['id'], 1)
        self.assertEqual(len(self.store.matches()), 25)
        self.reload()
        self.assertEqual(self.store.last()['id'], 1)

    def test_sessions_do_not_impose_count_limit(self):
        for i in range(15): self.store.add(ts=i)
        self.store.new_session()
        for i in range(15, 30): self.store.add(ts=i)
        ms = self.store.matches()
        self.assertEqual(len(ms), 30)
        self.assertEqual(sum(m['session'] == 1 for m in ms), 15)
        self.assertEqual(sum(m['session'] == 2 for m in ms), 15)

    def test_25_or_fewer_are_preserved(self):
        self.assertEqual(self.store.matches(), [])
        for i in range(25):
            self.store.add(ts=i)
            self.assertEqual(len(self.store.matches()), i+1)
        self.reload()
        self.assertEqual([m['id'] for m in self.store.matches()], list(range(1, 26)))
        self.assertEqual(os.listdir(self.tmp.name), ['history.json'])

    def test_retention_expires_old_rows_and_idle_pruning_works(self):
        now = time.time()
        self.store.add(ts=now-7200)
        self.store.add(ts=now-1800)
        self.store.add(ts=now-30)
        self.store.set_retention_hours(1, now=now)
        self.assertEqual([m['id'] for m in self.store.matches()], [2,3])
        self.reload()
        self.assertEqual([m['id'] for m in self.store.matches()], [2,3])
        self.store.prune(now=now+3601)
        self.assertEqual(self.store.matches(), [])
        self.assertEqual(self.saved()['next_id'], 4)
        self.assertEqual(os.listdir(self.tmp.name), ['history.json'])

    def test_zero_retention_does_not_expire_rows(self):
        self.store.add(ts=1)
        self.store.set_retention_hours(0)
        self.store.prune(now=time.time()+999999)
        self.assertEqual(len(self.store.matches()), 1)

    def test_character_change_clears_all_history_once_and_preserves_ids(self):
        self.store.add(ts=1, my_char='ryu')
        self.assertFalse(self.store.observe_character('ryu'))
        self.assertFalse(self.store.observe_character('ryu'))
        self.assertTrue(self.store.observe_character('juri'))
        self.assertEqual(self.store.matches(), [])
        self.assertEqual(self.saved()['session']['id'], 2)
        self.reload()
        self.assertFalse(self.store.observe_character('juri'))
        self.assertEqual(self.saved()['session']['id'], 2)
        rec = self.store.add(ts=2, my_char='juri')
        self.assertEqual(rec['id'], 2)
        self.assertEqual(os.listdir(self.tmp.name), ['history.json'])

    def test_character_change_after_manual_session_still_clears_history(self):
        self.store.add(ts=1, my_char='ryu')
        self.store.observe_character('ryu')
        self.store.new_session()
        self.assertTrue(self.store.observe_character('juri'))
        self.assertEqual(self.store.matches(), [])


if __name__ == '__main__': unittest.main()
