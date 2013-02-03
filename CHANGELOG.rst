======================
pyjournalctl changelog
======================

0.7.0
-----
* Removed ``data_threshold`` as creates incompatibility pre *systemd v196*
* Documentation tweaks - Added this CHANGELOG
* Error tweaks

0.6.1
-----
* Fix to function with python3

0.6.0
-----
* Added ``data_threshold`` attribute
* Big improvment on error checking and raising of python exceptions
* ``call_dict`` and ``default_call`` now have type checking and cannot be deleted

0.5.0
-----
* Can now handle multiple of the same field from journal
* Release python GIL on most C calls, particularly *wait*, allowing other python threads to execute
* Added a few more fields to default ``call_dict``

0.4.0
-----
* Added ``default_call`` and ``call_dict`` to assign functions to convert journal field values. Includes sane defaults which can be changed
* Seek monotonic and realtime accept ``timedelta`` and ``datatime`` instances

0.3.0
-----
* Added setting of ``sd_journal_open`` flags via ``__init__``
* Quick methods to add match for current *_BOOT_ID* and *_MACHINE_ID*
* Big improvement for docstrings

0.2.0
-----
* Added ``query_unique`` method
* *__REALTIME* and *__MONOTONIC* fields returned from ``get_next`` and associated seek functions
* *__CURSOR* field returned by ``get_next``
* Added ``log_level`` method
* Fix some python reference counts

0.1.1
-----
* *MANIFEST.in* added for uploading *README.rst* to *pypi*

0.1.0
-----
* ``pyjournalctl`` created
