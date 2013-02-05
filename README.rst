============
pyjournalctl
============

A python module that reads systemd journal similar to journalctl

Requirements
------------
- python2 >= 2.7 or python3
- systemd >= 187

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
>>> journal.add_match(PRIORITY="5", _PID="1")
>>> entry = journal.get_next()
>>> print("PRIORITY: %(PRIORITY)i" % entry)
PRIORITY: 5
>>> print("_PID: %(_PID)i" % entry)
_PID: 1
>>> print("MESSAGE: %(MESSAGE)s" % entry) # doctest: +ELLIPSIS
MESSAGE: ...
>>>
>>> journal.flush_matches()
>>> journal.add_messageid_match(pyjournalctl.SD_MESSAGE_JOURNAL_START)
>>> print(journal.get_next()['MESSAGE'])
Journal started
>>> journal.flush_matches()
>>> journal.seek(100) # 100 entries from start
>>> journal.add_match("_TRANSPORT=kernel")
>>> journal.add_disjunction() # OR next matches
>>> journal.add_match("PRIORITY=5")
>>> journal.add_match("_UID=0")
>>> entry = journal.get_next(2) # Get second match
>>> entry.get("_TRANSPORT") == "kernel" or (
...     entry.get('PRIORITY') == 5 and entry.get("_UID") == 0)
True
>>>
>>> cursor = entry['__CURSOR'] # Cursor is unique reference
>>> journal.flush_matches()
>>> import os
>>> journal.seek(0, os.SEEK_END) # End of journal
>>> entry2 = journal.get_previous()
>>> entry2['__CURSOR'] == cursor
False
>>> entry2 == entry
False
>>> journal.seek_cursor(cursor) # Seek to unique reference
>>> journal.get_next() == entry
True
>>> realtime = entry['__REALTIME_TIMESTAMP']
>>> journal.get_next(10) == entry
False
>>> journal.seek_realtime(realtime)
>>> journal.get_next() == entry
True
>>> monotonic = entry['__MONOTONIC_TIMESTAMP']
>>> bootid = entry['_BOOT_ID']
>>> journal.get_next(5) == entry
False
>>> journal.add_match(_BOOT_ID=bootid)
>>> journal.seek_monotonic(monotonic.total_seconds(), bootid)
>>> journal.get_next() == entry
True
>>> journal.flush_matches()
>>> journal.seek(-1000, os.SEEK_END) # Last 1000 entries
>>> priorities = set(range(0,5))
>>> journal.log_level(4) # Log level from 0 - 4
>>> priorities >= set(entry['PRIORITY'] for entry in journal)
True
>>> systemd_units = journal.query_unique("_SYSTEMD_UNIT")
>>> print("Unique systemd units in journal: %s" % ', '.join(systemd_units)) # doctest: +ELLIPSIS
Unique systemd units in journal: ...
>>> journal.flush_matches()
>>> journal.this_boot() # Only log entries from this boot
>>> journal.seek(0) # First entry
>>> entry = journal.get_next()
>>> journal.seek(0, os.SEEK_END) # Last entry
>>> journal.get_previous()['_BOOT_ID'] == entry['_BOOT_ID']
True
>>> journal.flush_matches()
>>> journal.seek(-1000, os.SEEK_END) # Last 1000 entries
>>> journal.this_machine() # Only log entries for this machine
>>> len(set(entry['_MACHINE_ID'] for entry in journal))
1

Known Issues
------------

TODO
----
