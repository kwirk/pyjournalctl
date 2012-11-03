============
pyjournalctl
============

A python module that reads systemd journald similar to journalctl

Requirements
------------
- python2 >= 2.6 or python3
- systemd >= 194

Installation
------------
python setup.py install

License
-------
GNU Lesser General Public License v2.1

Usage Examples
--------------
>>> import pyjournalctl
>>> journal = pyjournalctl.Journalctl()
>>> journal.add_matches({"PRIORITY": "5", "_PID": "1"})
>>> entry = journal.get_next()
>>> "PRIORITY: %(PRIORITY)s" % entry
'PRIORITY: 5'
>>> "_PID: %(_PID)s" % entry
'_PID: 1'
>>> "MESSAGE: %(MESSAGE)s" % entry # doctest: +ELLIPSIS
'MESSAGE: ...'
>>>
>>> journal.flush_matches()
>>> journal.seek(100) # 100 entries from start
>>> journal.add_match("_TRANSPORT", "kernel")
>>> journal.add_disjunction() # OR next matches
>>> journal.add_match("PRIORITY", "5")
>>> journal.add_match("_UID", "0")
>>> entry = journal.get_next(2) # Get second match
>>> entry.get("_TRANSPORT") == "kernel" or (
...     entry.get('PRIORITY') == "5" and entry.get("_UID") == "0")
True
>>>
>>> cursor = journal.get_cursor() # Cursor is unique reference
>>> cursor # doctest: +ELLIPSIS
'...'
>>> journal.flush_matches()
>>> journal.seek(0,2) # End of journal
>>> entry2 = journal.get_previous()
>>> journal.get_cursor() == cursor
False
>>> entry2 == entry
False
>>> journal.seek_cursor(cursor) # Seek to unique reference
>>> journal.get_next() == entry
True
>>> journal.get_cursor() == cursor
>>> realtime = int(entry['__REALTIME_TIMESTAMP'])
>>> journal.get_next(10) == entry
False
>>> journal.seek_realtime(realtime)
>>> journal.get_next() == entry
True
>>> monotonic = int(entry['__MONOTONIC_TIMESTAMP'])
>>> bootid = entry['_BOOT_ID']
>>> journal.get_next(5) == entry
False
>>> journal.seek_monotonic(monotonic, bootid)
>>> journal.get_next() == entry
True
>>> journal.flush_matches()
>>> journal.seek(-1000,2) # Last 1000 entries
>>> priorities = set(range(2,6))
>>> for priority in priorities:
...     # Items of the same field name are automatically 'or'ed
...     journal.add_match("PRIORITY=%i" % priority)
>>> priorities >= set(int(entry['PRIORITY']) for entry in journal)
True
>>> systemd_units = journal.query_unique("_SYSTEMD_UNIT")
>>> "Unique systemd units in journal: %s" % ', '.join(systemd_units) # doctest: +ELLIPSIS
'Unique systemd units in journal: ...'
>>> len(systemd_units) == len(set(systemd_units))
True
