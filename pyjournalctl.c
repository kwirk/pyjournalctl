/*
pyjournalctl - Python module that reads systemd journal similar to journalctl
Copyright (C) 2012  Steven Hiscocks

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <systemd/sd-journal.h>
#include <systemd/sd-messages.h>

#include <Python.h>
#include <structmember.h>
#include <datetime.h>

typedef struct {
    PyObject_HEAD
    sd_journal *j;
    PyObject *default_call;
    PyObject *call_dict;
} Journalctl;
static PyTypeObject JournalctlType;

static void
Journalctl_dealloc(Journalctl* self)
{
    sd_journal_close(self->j);
    Py_XDECREF(self->default_call);
    Py_XDECREF(self->call_dict);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
Journalctl_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    Journalctl *self;

    self = (Journalctl *)type->tp_alloc(type, 0);
    if (self != NULL) {
        PyObject *globals, *temp;

        globals = PyEval_GetBuiltins();
        temp = PyImport_ImportModule("functools");
        PyDict_SetItemString(globals, "functools", temp);
        Py_DECREF(temp);
        temp = PyImport_ImportModule("datetime");
        PyDict_SetItemString(globals, "datetime", temp);
        Py_DECREF(temp);

#if PY_MAJOR_VERSION >=3
        self->default_call = PyRun_String("functools.partial(str, encoding='utf-8')", Py_eval_input, globals, NULL);
#else
        self->default_call = PyRun_String("functools.partial(unicode, encoding='utf-8')", Py_eval_input, globals, NULL);
#endif

        self->call_dict = PyRun_String("{"
            "'PRIORITY': int,"
            "'LEADER': int,"
            "'SESSION_ID': int,"
            "'USERSPACE_USEC': int,"
            "'INITRD_USEC': int,"
            "'KERNEL_USEC': int,"
            "'_UID': int,"
            "'_GID': int,"
            "'_PID': int,"
            "'SYSLOG_FACILITY': int,"
            "'SYSLOG_PID': int,"
            "'_AUDIT_SESSION': int,"
            "'_AUDIT_LOGINUID': int,"
            "'_SYSTEMD_SESSION': int,"
            "'_SYSTEMD_OWNER_UID': int,"
            "'CODE_LINE': int,"
            "'ERRNO': int,"
            "'EXIT_STATUS': int,"
            "'_SOURCE_REALTIME_TIMESTAMP': lambda x: datetime.datetime.fromtimestamp(float(x)/1E6),"
            "'__REALTIME_TIMESTAMP': lambda x: datetime.datetime.fromtimestamp(float(x)/1E6),"
            "'_SOURCE_MONOTONIC_TIMESTAMP': lambda x: datetime.timedelta(microseconds=float(x)),"
            "'__MONOTONIC_TIMESTAMP': lambda x: datetime.timedelta(microseconds=float(x)),"
#if PY_MAJOR_VERSION >=3
            "'COREDUMP': bytes,"
#else
            "'COREDUMP': str,"
#endif
            "'COREDUMP_PID': int,"
            "'COREDUMP_UID': int,"
            "'COREDUMP_GID': int,"
            "'COREDUMP_SESSION': int,"
            "'COREDUMP_SIGNAL': int,"
            "'COREDUMP_TIMESTAMP': lambda x: datetime.datetime.fromtimestamp(float(x)/1E6),"
            "}", Py_eval_input, globals, NULL);
    }

    return (PyObject *) self;
}

PyDoc_STRVAR(Journalctl__doc__,
"Journalctl([flags][, default_call][, call_dict][,path]) -> Journalctl instance\n\n"
"Returns instance of Journalctl, which allows filtering and return\n"
"of journal entries.\n"
"Argument `flags` sets open flags of the journal, which can be one\n"
"of, or ORed combination of: SD_JOURNAL_LOCAL_ONLY (default) opens\n"
"journal on local machine only; SD_JOURNAL_RUNTIME_ONLY opens only\n"
"volatile journal files; and SD_JOURNAL_SYSTEM_ONLY opens only\n"
"journal files of system services and the kernel.\n"
"Argument `default_call` must be a callable that accepts one\n"
"argument which is string/bytes value of a field and returns\n"
"python object.\n"
"Argument `call_dict` is a dictionary where the key represents\n"
"a field name, and value is a callable as per `default_call`.\n"
"A set of sane defaults for `default_call` and `call_dict` are\n"
"present.\n"
"Argument `path` is the directory of journal files. Note that\n"
"currently flags are ignored when `path` is present as they are\n"
" not relevant.");
static int
Journalctl_init(Journalctl *self, PyObject *args, PyObject *keywds)
{
    int flags=SD_JOURNAL_LOCAL_ONLY;
    char *path=NULL;
    PyObject *default_call=NULL, *call_dict=NULL;

    static char *kwlist[] = {"flags", "default_call", "call_dict", "path", NULL};
    if (! PyArg_ParseTupleAndKeywords(args, keywds, "|iOOs", kwlist,
                                      &flags, &default_call, &call_dict, &path))
        return 1;

    if (default_call) {
        if (PyCallable_Check(default_call) || default_call == Py_None) {
            Py_DECREF(self->default_call);
            self->default_call = default_call;
            Py_INCREF(self->default_call);
        }else{
            PyErr_SetString(PyExc_TypeError, "Default call not callable");
            return 1;
        }
    }

    if (call_dict) {
        if (PyDict_Check(call_dict)) {
            Py_DECREF(self->call_dict);
            self->call_dict = call_dict;
            Py_INCREF(self->call_dict);
        }else if (call_dict == Py_None) {
            Py_DECREF(self->call_dict);
            self->call_dict = PyDict_New();
        }else{
            PyErr_SetString(PyExc_TypeError, "Call dictionary must be dict type");
            return 1;
        }
    }

    int r;
    if (path) {
        r = sd_journal_open_directory(&self->j, path, 0);
    }else{
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_open(&self->j, flags);
        Py_END_ALLOW_THREADS
    }
    if (r == -EINVAL) {
        PyErr_SetString(PyExc_ValueError, "Invalid flags or path");
        return -1;
    }else if (r == -ENOMEM) {
        PyErr_SetString(PyExc_MemoryError, "Not enough memory");
        return 1;
    }else if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error opening journal");
        return 1;
    }

    return 0;
}

static PyObject *
Journalctl___process_field(Journalctl *self, PyObject *key, const void *value, ssize_t value_len)
{
    PyObject *callable=NULL, *return_value=NULL;
    if (PyDict_Check(self->call_dict))
        callable = PyDict_GetItem(self->call_dict, key);

    if (PyCallable_Check(callable)) {
#if PY_MAJOR_VERSION >=3
        return_value = PyObject_CallFunction(callable, "y#", value, value_len);
#else
        return_value = PyObject_CallFunction(callable, "s#", value, value_len);
#endif
        if (!return_value)
            PyErr_Clear();
    }
    if (!return_value && PyCallable_Check(self->default_call))
#if PY_MAJOR_VERSION >=3
        return_value = PyObject_CallFunction(self->default_call, "y#", value, value_len);
#else
        return_value = PyObject_CallFunction(self->default_call, "s#", value, value_len);
#endif
    if (!return_value) {
        PyErr_Clear();
#if PY_MAJOR_VERSION >=3
        return_value = PyBytes_FromStringAndSize(value, value_len);
#else
        return_value = PyString_FromStringAndSize(value, value_len);
#endif
    }
    if (!return_value) {
        return_value = Py_None;
    }
    return return_value;
}

PyDoc_STRVAR(Journalctl_get_next__doc__,
"get_next([skip]) -> dict\n\n"
"Return dictionary of the next log entry. Optional skip value will\n"
"return the `skip`th log entry.");
static PyObject *
Journalctl_get_next(Journalctl *self, PyObject *args)
{
    int64_t skip=1LL;
    if (! PyArg_ParseTuple(args, "|L", &skip))
        return NULL;

    int r;
    if (skip == 1LL) {
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_next(self->j);
        Py_END_ALLOW_THREADS
    }else if (skip == -1LL) {
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_previous(self->j);
        Py_END_ALLOW_THREADS
    }else if (skip > 1LL) {
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_next_skip(self->j, skip);
        Py_END_ALLOW_THREADS
    }else if (skip < -1LL) {
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_previous_skip(self->j, -skip);
        Py_END_ALLOW_THREADS
    }else{
        PyErr_SetString(PyExc_ValueError, "Skip number must positive/negative integer");
        return NULL;
    }

    if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error getting next message");
        return NULL;
    }else if ( r == 0) { //EOF
        return PyDict_New();
    }

    PyObject *dict;
    dict = PyDict_New();

    const void *msg;
    size_t msg_len;
    const char *delim_ptr;
    PyObject *key, *value, *cur_value, *tmp_list;

    SD_JOURNAL_FOREACH_DATA(self->j, msg, msg_len) {
        delim_ptr = memchr(msg, '=', msg_len);
#if PY_MAJOR_VERSION >=3
        key = PyUnicode_FromStringAndSize(msg, delim_ptr - (const char*) msg);
#else
        key = PyString_FromStringAndSize(msg, delim_ptr - (const char*) msg);
#endif
        value = Journalctl___process_field(self, key, delim_ptr + 1, (const char*) msg + msg_len - (delim_ptr + 1) );
        if (PyDict_Contains(dict, key)) {
            cur_value = PyDict_GetItem(dict, key);
            if (PyList_CheckExact(cur_value) && PyList_Size(cur_value) > 1) {
                PyList_Append(cur_value, value);
            }else{
                tmp_list = PyList_New(0);
                PyList_Append(tmp_list, cur_value);
                PyList_Append(tmp_list, value);
                PyDict_SetItem(dict, key, tmp_list);
                Py_DECREF(tmp_list);
            }
        }else{
            PyDict_SetItem(dict, key, value);
        }
        Py_DECREF(key);
        Py_DECREF(value);
    }

    uint64_t realtime;
    if (sd_journal_get_realtime_usec(self->j, &realtime) == 0) {
        char realtime_str[20];
        sprintf(realtime_str, "%llu", (long long unsigned) realtime);

#if PY_MAJOR_VERSION >=3
        key = PyUnicode_FromString("__REALTIME_TIMESTAMP");
#else
        key = PyString_FromString("__REALTIME_TIMESTAMP");
#endif
        value = Journalctl___process_field(self, key, realtime_str, strlen(realtime_str));
        PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
    }

    sd_id128_t sd_id;
    uint64_t monotonic;
    if (sd_journal_get_monotonic_usec(self->j, &monotonic, &sd_id) == 0) {
        char monotonic_str[20];
        sprintf(monotonic_str, "%llu", (long long unsigned) monotonic);
#if PY_MAJOR_VERSION >=3
        key = PyUnicode_FromString("__MONOTONIC_TIMESTAMP");
#else
        key = PyString_FromString("__MONOTONIC_TIMESTAMP");
#endif
        value = Journalctl___process_field(self, key, monotonic_str, strlen(monotonic_str));

        PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
    }

    char *cursor;
    if (sd_journal_get_cursor(self->j, &cursor) > 0) { //Should return 0...
#if PY_MAJOR_VERSION >=3
        key = PyUnicode_FromString("__CURSOR");
#else
        key = PyString_FromString("__CURSOR");
#endif
        value = Journalctl___process_field(self, key, cursor, strlen(cursor));
        PyDict_SetItem(dict, key, value);
        free(cursor);
        Py_DECREF(key);
        Py_DECREF(value);
    }

    return dict;
}

PyDoc_STRVAR(Journalctl_get_previous__doc__,
"get_previous([skip]) -> dict\n\n"
"Return dictionary of the previous log entry. Optional skip value\n"
"will return the -`skip`th log entry. Equivalent to get_next(-skip).");
static PyObject *
Journalctl_get_previous(Journalctl *self, PyObject *args)
{
    int64_t skip=1LL;
    if (! PyArg_ParseTuple(args, "|L", &skip))
        return NULL;

    PyObject *dict, *arg;
    arg = Py_BuildValue("(L)", -skip);
    dict = Journalctl_get_next(self, arg);
    Py_DECREF(arg);
    return dict;
}

PyDoc_STRVAR(Journalctl_add_match__doc__,
"add_match(match, ..., field=value, ...) -> None\n\n"
"Add a match to filter journal log entries. All matches of different\n"
"field are combined in logical AND, and matches of the same field\n"
"are automatically combined in logical OR.\n"
"Matches can be passed as strings \"field=value\", or keyword\n"
"arguments field=\"value\".");
static PyObject *
Journalctl_add_match(Journalctl *self, PyObject *args, PyObject *keywds)
{
    Py_ssize_t arg_match_len;
    char *arg_match;
    int i, r;
    for (i = 0; i < PySequence_Size(args); i++) {
#if PY_MAJOR_VERSION >=3
        PyObject *arg;
        arg = PySequence_Fast_GET_ITEM(args, i);
        if (PyUnicode_Check(arg)) {
#if PY_MINOR_VERSION >=3
            arg_match = PyUnicode_AsUTF8AndSize(arg, &arg_match_len);
#else
            PyObject *temp;
            temp = PyUnicode_AsUTF8String(arg);
            PyBytes_AsStringAndSize(temp, &arg_match, &arg_match_len);
            Py_DECREF(temp);
#endif
        }else if (PyBytes_Check(arg)) {
            PyBytes_AsStringAndSize(arg, &arg_match, &arg_match_len);
        }else{
            PyErr_SetString(PyExc_TypeError, "expected bytes or string");
        }
#else
        PyString_AsStringAndSize(PySequence_Fast_GET_ITEM(args, i), &arg_match, &arg_match_len);
#endif
        if (PyErr_Occurred())
            return NULL;
        r = sd_journal_add_match(self->j, arg_match, arg_match_len);
        if (r == -EINVAL) {
            PyErr_SetString(PyExc_ValueError, "Invalid match");
            return NULL;
        }else if (r == -ENOMEM) {
            PyErr_SetString(PyExc_MemoryError, "Not enough memory");
            return NULL;
        }else if (r < 0) {
            PyErr_SetString(PyExc_RuntimeError, "Error adding match");
            return NULL;
        }
    }

    if (! keywds)
        Py_RETURN_NONE;

    PyObject *key, *value;
    Py_ssize_t pos=0, match_key_len, match_value_len;
    int match_len;
    char *match_key, *match_value;
    void *match;
    while (PyDict_Next(keywds, &pos, &key, &value)) {
#if PY_MAJOR_VERSION >=3
        if (PyUnicode_Check(key)) {
#if PY_MINOR_VERSION >=3
            match_key = PyUnicode_AsUTF8AndSize(key, &match_key_len);
#else
            PyObject *temp2;
            temp2 = PyUnicode_AsUTF8String(key);
            PyBytes_AsStringAndSize(temp2, &match_key, &match_key_len);
            Py_DECREF(temp2);
#endif
        }else if (PyBytes_Check(key)) {
            PyBytes_AsStringAndSize(key, &match_key, &match_key_len);
        }else{
            PyErr_SetString(PyExc_TypeError, "expected bytes or string");
        }
        if (PyUnicode_Check(value)) {
#if PY_MINOR_VERSION >=3
            match_value = PyUnicode_AsUTF8AndSize(value, &match_value_len);
#else
            PyObject *temp3;
            temp3 = PyUnicode_AsUTF8String(value);
            PyBytes_AsStringAndSize(temp3, &match_value, &match_value_len);
            Py_DECREF(temp3);
#endif
        }else if (PyBytes_Check(value)) {
            PyBytes_AsStringAndSize(value, &match_value, &match_value_len);
        }else{
            PyErr_SetString(PyExc_TypeError, "expected bytes or string");
        }
#else
        PyString_AsStringAndSize(key, &match_key, &match_key_len);
        PyString_AsStringAndSize(value, &match_value, &match_value_len);
#endif
        if (PyErr_Occurred())
            return NULL;

        match_len = match_key_len + 1 + match_value_len;
        match = malloc(match_len);
        memcpy(match, match_key, match_key_len);
        memcpy(match + match_key_len, "=", 1);
        memcpy(match + match_key_len + 1, match_value, match_value_len);

        r = sd_journal_add_match(self->j, match, match_len);
        free(match);
        if (r == -EINVAL) {
            PyErr_SetString(PyExc_ValueError, "Invalid match");
            return NULL;
        }else if (r == -ENOMEM) {
            PyErr_SetString(PyExc_MemoryError, "Not enough memory");
            return NULL;
        }else if (r < 0) {
            PyErr_SetString(PyExc_RuntimeError, "Error adding match");
            return NULL;
        }
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_add_messageid_match__doc__,
"add_messageid_match(messageid) -> None\n\n"
"Equivalent to add_match(\"MESSAGE_ID\", messageid).\n"
"Allows easier adding of match for SD_MESSAGE_* constants.");
static PyObject *
Journalctl_add_messageid_match(Journalctl *self, PyObject *args)
{
    PyObject *value;
    if (! PyArg_ParseTuple(args, "O", &value))
        return NULL;

    PyObject *arg, *keywds;
    arg = PyTuple_New(0);
    keywds = Py_BuildValue("{s:O}", "MESSAGE_ID", value);
    Journalctl_add_match(self, arg, keywds);
    Py_DECREF(arg);
    Py_DECREF(keywds);
    if (PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_add_disjunction__doc__,
"add_disjunction() -> None\n\n"
"Once called, all matches before and after are combined in logical\n"
"OR.");
static PyObject *
Journalctl_add_disjunction(Journalctl *self, PyObject *args)
{
    int r;
    r = sd_journal_add_disjunction(self->j);
    if (r == -ENOMEM) {
        PyErr_SetString(PyExc_MemoryError, "Not enough memory");
        return NULL;
    }else if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error adding disjunction");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_flush_matches__doc__,
"flush_matches() -> None\n\n"
"Clears all current match filters.");
static PyObject *
Journalctl_flush_matches(Journalctl *self, PyObject *args)
{
    sd_journal_flush_matches(self->j);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_seek__doc__,
"seek(offset[, whence]) -> None\n\n"
"Seek through journal by `offset` number of entries. Argument\n"
"`whence` defines what the offset is relative to: 0 (default) is\n"
"from first match in journal; 1 from current position; and 2 is from\n"
"last match in journal.");
static PyObject *
Journalctl_seek(Journalctl *self, PyObject *args, PyObject *keywds)
{
    int64_t offset;
    int whence=SEEK_SET;
    static char *kwlist[] = {"offset", "whence", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, keywds, "L|i", kwlist,
                                      &offset, &whence))
        return NULL;

    PyObject *arg;
    if (whence == SEEK_SET){
        int r;
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_seek_head(self->j);
        Py_END_ALLOW_THREADS
        if (r < 0) {
            PyErr_SetString(PyExc_RuntimeError, "Error seeking to head");
            return NULL;
        }
        if (offset > 0LL) {
            arg = Py_BuildValue("(L)", offset);
            Py_DECREF(Journalctl_get_next(self, arg));
            Py_DECREF(arg);
        }
    }else if (whence == SEEK_CUR){
        arg = Py_BuildValue("(L)", offset);
        Py_DECREF(Journalctl_get_next(self, arg));
        Py_DECREF(arg);
    }else if (whence == SEEK_END){
        int r;
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_seek_tail(self->j);
        Py_END_ALLOW_THREADS
        if (r < 0) {
            PyErr_SetString(PyExc_RuntimeError, "Error seeking to tail");
            return NULL;
        }
        arg = Py_BuildValue("(L)", -1LL);
        Py_DECREF(Journalctl_get_next(self, arg));
        Py_DECREF(arg);
        if (offset < 0LL) {
            arg = Py_BuildValue("(L)", offset);
            Py_DECREF(Journalctl_get_next(self, arg));
            Py_DECREF(arg);
        }
    }else{
        PyErr_SetString(PyExc_ValueError, "Invalid value for whence");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_seek_realtime__doc__,
"seek_realtime(realtime) -> None\n\n"
"Seek to nearest matching journal entry to `realtime`. Argument\n"
"`realtime` can be an integer unix timestamp in usecs or a "
"datetime instance.");
static PyObject *
Journalctl_seek_realtime(Journalctl *self, PyObject *args)
{
    PyObject *arg;
    if (! PyArg_ParseTuple(args, "O", &arg))
        return NULL;

    uint64_t timestamp=-1LL;
    if (PyDateTime_Check(arg)) {
        PyObject *temp;
        char *timestamp_str;
        temp = PyObject_CallMethod(arg, "strftime", "s", "%s%f");
#if PY_MAJOR_VERSION >=3
        PyObject *temp2;
        temp2 = PyUnicode_AsUTF8String(temp);
        timestamp_str = PyBytes_AsString(temp2);
        Py_DECREF(temp2);
#else
        timestamp_str = PyString_AsString(temp);
#endif
        Py_DECREF(temp);
        timestamp = strtoull(timestamp_str, NULL, 10);
    }else if (PyLong_Check(arg)) {
        timestamp = PyLong_AsUnsignedLongLong(arg);
#if PY_MAJOR_VERSION <3
    }else if (PyInt_Check(arg)) {
        timestamp = PyInt_AsUnsignedLongLongMask(arg);
#endif
    }
    if ((int64_t) timestamp < 0LL) {
        PyErr_SetString(PyExc_ValueError, "Time must be positive integer or datetime instance");
        return NULL;
    }

    int r;
    Py_BEGIN_ALLOW_THREADS
    r = sd_journal_seek_realtime_usec(self->j, timestamp);
    Py_END_ALLOW_THREADS
    if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error seek to time");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_seek_monotonic__doc__,
"seek_monotonic(monotonic[, bootid]) -> None\n\n"
"Seek to nearest matching journal entry to `monotonic`. Argument\n"
"`monotonic` is an timestamp from boot in secs, or a\n"
"timedelta instance.\n"
"Argument `bootid` is a string representing which boot the\n"
"monotonic time is reference to. Defaults to current bootid.");
static PyObject *
Journalctl_seek_monotonic(Journalctl *self, PyObject *args)
{
    PyObject *arg;
    char *bootid=NULL;
    if (! PyArg_ParseTuple(args, "O|s", &arg, &bootid))
        return NULL;

    uint64_t timestamp=-1LL;
    if PyDelta_Check(arg) {
        PyObject *temp;
        temp = PyObject_CallMethod(arg, "total_seconds", NULL);
        timestamp = (uint64_t) (PyFloat_AsDouble(temp) * 1E6);
        Py_DECREF(temp);
    }else if (PyFloat_Check(arg)) {
        timestamp = (uint64_t) (PyFloat_AsDouble(arg) * 1E6);
    }else if (PyLong_Check(arg)) {
        timestamp = PyLong_AsUnsignedLongLong(arg) * (uint64_t) 1E6;
#if PY_MAJOR_VERSION <3
    }else if (PyInt_Check(arg)) {
        timestamp = PyInt_AsUnsignedLongLongMask(arg) * (uint64_t) 1E6;
#endif

    }

    if ((int64_t) timestamp < 0LL) {
        PyErr_SetString(PyExc_ValueError, "Time must be positive number or timedelta instance");
        return NULL;
    }

    sd_id128_t sd_id;
    int r;
    if (bootid) {
        r = sd_id128_from_string(bootid, &sd_id);
        if (r == -EINVAL) {
            PyErr_SetString(PyExc_ValueError, "Invalid bootid");
            return NULL;
        } else if (r < 0) {
            PyErr_SetString(PyExc_RuntimeError, "Error processing bootid");
            return NULL;
        }
    }else{
        r = sd_id128_get_boot(&sd_id);
        if (r == -EIO) {
            PyErr_SetString(PyExc_IOError, "Error getting current boot ID");
            return NULL;
        } else if (r < 0) {
            PyErr_SetString(PyExc_RuntimeError, "Error getting current boot ID");
            return NULL;
        }
    }

    Py_BEGIN_ALLOW_THREADS
    r = sd_journal_seek_monotonic_usec(self->j, sd_id, timestamp);
    Py_END_ALLOW_THREADS
    if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error seek to time");
        return NULL;
    }
    Py_RETURN_NONE;
}
 
PyDoc_STRVAR(Journalctl_wait__doc__,
"wait([timeout]) -> Change state (integer)\n\n"
"Waits until there is a change in the journal. Argument `timeout`\n"
"is the maximum number of seconds to wait before returning\n"
"regardless if journal has changed. If `timeout` is not given or is\n"
"0, then it will block forever.\n"
"Will return: SD_JOURNAL_NOP if no change; SD_JOURNAL_APPEND if new\n"
"entries have been added to the end of the journal; and\n"
"SD_JOURNAL_INVALIDATE if journal files have been added or removed.");
static PyObject *
Journalctl_wait(Journalctl *self, PyObject *args, PyObject *keywds)
{
    int64_t timeout=0LL;
    if (! PyArg_ParseTuple(args, "|L", &timeout))
        return NULL;

    int r;
    if ( timeout == 0LL) {
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_wait(self->j, (uint64_t) -1);
        Py_END_ALLOW_THREADS
    }else{
        Py_BEGIN_ALLOW_THREADS
        r = sd_journal_wait(self->j, timeout * 1E6);
        Py_END_ALLOW_THREADS
    }
#if PY_MAJOR_VERSION >=3
    return PyLong_FromLong(r);
#else
    return PyInt_FromLong(r);
#endif
}

PyDoc_STRVAR(Journalctl_seek_cursor__doc__,
"seek_cursor(cursor) -> None\n\n"
"Seeks to journal entry by given unique reference `cursor`.");
static PyObject *
Journalctl_seek_cursor(Journalctl *self, PyObject *args)
{
    const char *cursor;
    if (! PyArg_ParseTuple(args, "s", &cursor))
        return NULL;

    int r;
    Py_BEGIN_ALLOW_THREADS
    r = sd_journal_seek_cursor(self->j, cursor);
    Py_END_ALLOW_THREADS
    if (r == -EINVAL) {
        PyErr_SetString(PyExc_ValueError, "Invalid cursor");
        return NULL;
    }else if (r == -ENOMEM) {
        PyErr_SetString(PyExc_MemoryError, "Not enough memory");
        return NULL;
    }else if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error seeking to cursor");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Journalctl_iter(PyObject *self)
{
    Py_INCREF(self);
    return self;
}

static PyObject *
Journalctl_iternext(PyObject *self)
{
    Journalctl *iter = (Journalctl *)self;
    PyObject *dict, *arg;
    Py_ssize_t dict_size;

    arg =  Py_BuildValue("()");
    dict = Journalctl_get_next(iter, arg);
    Py_DECREF(arg);
    dict_size = PyDict_Size(dict);
    if ((int64_t) dict_size > 0LL) {
        return dict;
    }else{
        Py_DECREF(dict);
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }
}

#ifdef SD_JOURNAL_FOREACH_UNIQUE
PyDoc_STRVAR(Journalctl_query_unique__doc__,
"query_unique(field) -> a set of values\n\n"
"Returns a set of unique values in journal for given `field`.\n"
"Note this does not respect any journal matches.");
static PyObject *
Journalctl_query_unique(Journalctl *self, PyObject *args)
{
    char *query;
    if (! PyArg_ParseTuple(args, "s", &query))
        return NULL;

    int r;
    Py_BEGIN_ALLOW_THREADS
    r = sd_journal_query_unique(self->j, query);
    Py_END_ALLOW_THREADS
    if (r == -EINVAL) {
        PyErr_SetString(PyExc_ValueError, "Invalid field name");
        return NULL;
    } else if (r == -ENOMEM) {
        PyErr_SetString(PyExc_MemoryError, "Not enough memory");
        return NULL;
    } else if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error querying journal");
        return NULL;
    }

    const void *uniq;
    size_t uniq_len;
    const char *delim_ptr;
    PyObject *value_set, *key, *value;
    value_set = PySet_New(0);

#if PY_MAJOR_VERSION >=3
    key = PyUnicode_FromString(query);
#else
    key = PyString_FromString(query);
#endif

    SD_JOURNAL_FOREACH_UNIQUE(self->j, uniq, uniq_len) {
        delim_ptr = memchr(uniq, '=', uniq_len);
        value = Journalctl___process_field(self, key, delim_ptr + 1, (const char*) uniq + uniq_len - (delim_ptr + 1));
        PySet_Add(value_set, value);
        Py_DECREF(value);
    }
    Py_DECREF(key);
    return value_set;
}
#endif //def SD_JOURNAL_FOREACH_UNIQUE

PyDoc_STRVAR(Journalctl_log_level__doc__,
"log_level(level) -> None\n\n"
"Sets maximum log level by setting matches for PRIORITY.");
static PyObject *
Journalctl_log_level(Journalctl *self, PyObject *args)
{
    int level;
    if (! PyArg_ParseTuple(args, "i", &level))
        return NULL;

    if (level < 0 || level > 7) {
        PyErr_SetString(PyExc_ValueError, "Log level should be 0 <= level <= 7");
        return NULL;
    }
    int i;
    char level_str[2];
    PyObject *arg, *keywds;
    for(i = 0; i <= level; i++) {
        sprintf(level_str, "%i", i);
        arg = PyTuple_New(0);
        keywds = Py_BuildValue("{s:s}", "PRIORITY", level_str);
        Journalctl_add_match(self, arg, keywds);
        Py_DECREF(arg);
        Py_DECREF(keywds);
        if (PyErr_Occurred())
            return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_this_boot__doc__,
"this_boot() -> None\n\n"
"Sets match filter for the current _BOOT_ID.");
static PyObject *
Journalctl_this_boot(Journalctl *self, PyObject *args)
{
    sd_id128_t sd_id;
    int r;
    r = sd_id128_get_boot(&sd_id);
    if (r == -EIO) {
        PyErr_SetString(PyExc_IOError, "Error getting current boot ID");
        return NULL;
    } else if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error getting current boot ID");
        return NULL;
    }

    char bootid[33];
    sd_id128_to_string(sd_id, bootid);

    PyObject *arg, *keywds;
    arg = PyTuple_New(0);
    keywds = Py_BuildValue("{s:s}", "_BOOT_ID", bootid);
    Journalctl_add_match(self, arg, keywds);
    Py_DECREF(arg);
    Py_DECREF(keywds);
    if (PyErr_Occurred())
        return NULL;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(Journalctl_this_machine__doc__,
"this_machine() -> None\n\n"
"Sets match filter for the current _MACHINE_ID.");
static PyObject *
Journalctl_this_machine(Journalctl *self, PyObject *args)
{
    sd_id128_t sd_id;
    int r;
    r = sd_id128_get_machine(&sd_id);
    if (r == -EIO) {
        PyErr_SetString(PyExc_IOError, "Error getting current boot ID");
        return NULL;
    } else if (r < 0) {
        PyErr_SetString(PyExc_RuntimeError, "Error getting current boot ID");
        return NULL;
    }

    char machineid[33];
    sd_id128_to_string(sd_id, machineid);

    PyObject *arg, *keywds;
    arg = PyTuple_New(0);
    keywds = Py_BuildValue("{s:s}", "_MACHINE_ID", machineid);
    Journalctl_add_match(self, arg, keywds);
    Py_DECREF(arg);
    Py_DECREF(keywds);
    if (PyErr_Occurred())
        return NULL;

    Py_RETURN_NONE;
}

static PyObject *
Journalctl_get_default_call(Journalctl *self, void *closure)
{
    Py_INCREF(self->default_call);
    return self->default_call;
}

static int
Journalctl_set_default_call(Journalctl *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete default_call");
        return -1;
    }
    if (! PyCallable_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "default_call must be callable");
        return -1;
    }
    Py_DECREF(self->default_call);
    Py_INCREF(value);
    self->default_call = value;

    return 0;
}

static PyObject *
Journalctl_get_call_dict(Journalctl *self, void *closure)
{
    Py_INCREF(self->call_dict);
    return self->call_dict;
}

static int
Journalctl_set_call_dict(Journalctl *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete call_dict");
        return -1;
    }
    if (! PyDict_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "call_dict must be dict type");
        return -1;
    }
    Py_DECREF(self->call_dict);
    Py_INCREF(value);
    self->call_dict = value;

    return 0;
}

/*static PyObject *
Journalctl_get_data_threshold(Journalctl *self, void *closure)
{
    size_t cvalue;
    PyObject *value;
    int r;

    r = sd_journal_get_data_threshold(self->j, &cvalue);
    if (r < 0){
        PyErr_SetString(PyExc_RuntimeError, "Error getting data threshold");
        return NULL;
    }

#if PY_MAJOR_VERSION >=3
    value = PyLong_FromSize_t(cvalue);
#else
    value = PyInt_FromSize_t(cvalue);
#endif
    return value;
}

static int
Journalctl_set_data_threshold(Journalctl *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "Cannot delete data threshold");
        return -1;
    }
#if PY_MAJOR_VERSION >=3
    if (! PyLong_Check(value)){
#else
    if (! PyInt_Check(value)){
#endif
        PyErr_SetString(PyExc_TypeError, "Data threshold must be int");
        return -1;
    }
    int r;
#if PY_MAJOR_VERSION >=3
    r = sd_journal_set_data_threshold(self->j, (size_t) PyLong_AsLong(value));
#else
    r = sd_journal_set_data_threshold(self->j, (size_t) PyInt_AsLong(value));
#endif
    if (r < 0){
        PyErr_SetString(PyExc_RuntimeError, "Error setting data threshold");
        return -1;
    }
    return 0;
} */

static PyGetSetDef Journalctl_getseters[] = {
/*    {"data_threshold",
    (getter)Journalctl_get_data_threshold,
    (setter)Journalctl_set_data_threshold,
    "data threshold",
    NULL},*/
    {"call_dict",
    (getter)Journalctl_get_call_dict,
    (setter)Journalctl_set_call_dict,
    "dictionary of calls for each field",
    NULL},
    {"default_call",
    (getter)Journalctl_get_default_call,
    (setter)Journalctl_set_default_call,
    "default call for values for fields",
    NULL},
    {NULL}
};

static PyMethodDef Journalctl_methods[] = {
    {"get_next", (PyCFunction)Journalctl_get_next, METH_VARARGS,
    Journalctl_get_next__doc__},
    {"get_previous", (PyCFunction)Journalctl_get_previous, METH_VARARGS,
    Journalctl_get_previous__doc__},
    {"add_match", (PyCFunction)Journalctl_add_match, METH_VARARGS|METH_KEYWORDS,
    Journalctl_add_match__doc__},
    {"add_messageid_match", (PyCFunction)Journalctl_add_messageid_match,
    METH_VARARGS, Journalctl_add_messageid_match__doc__},
    {"add_disjunction", (PyCFunction)Journalctl_add_disjunction, METH_NOARGS,
    Journalctl_add_disjunction__doc__},
    {"flush_matches", (PyCFunction)Journalctl_flush_matches, METH_NOARGS,
    Journalctl_flush_matches__doc__},
    {"seek", (PyCFunction)Journalctl_seek, METH_VARARGS | METH_KEYWORDS,
    Journalctl_seek__doc__},
    {"seek_realtime", (PyCFunction)Journalctl_seek_realtime, METH_VARARGS,
    Journalctl_seek_realtime__doc__},
    {"seek_monotonic", (PyCFunction)Journalctl_seek_monotonic, METH_VARARGS,
    Journalctl_seek_monotonic__doc__},
    {"wait", (PyCFunction)Journalctl_wait, METH_VARARGS,
    Journalctl_wait__doc__},
    {"seek_cursor", (PyCFunction)Journalctl_seek_cursor, METH_VARARGS,
    Journalctl_seek_cursor__doc__},
#ifdef SD_JOURNAL_FOREACH_UNIQUE
    {"query_unique", (PyCFunction)Journalctl_query_unique, METH_VARARGS,
    Journalctl_query_unique__doc__},
#endif
    {"log_level", (PyCFunction)Journalctl_log_level, METH_VARARGS,
    Journalctl_log_level__doc__},
    {"this_boot", (PyCFunction)Journalctl_this_boot, METH_NOARGS,
    Journalctl_this_boot__doc__},
    {"this_machine", (PyCFunction)Journalctl_this_machine, METH_NOARGS,
    Journalctl_this_machine__doc__},
    {NULL}  /* Sentinel */
};

static PyTypeObject JournalctlType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pyjournalctl.Journalctl",        /*tp_name*/
    sizeof(Journalctl),               /*tp_basicsize*/
    0,                                /*tp_itemsize*/
    (destructor)Journalctl_dealloc,   /*tp_dealloc*/
    0,                                /*tp_print*/
    0,                                /*tp_getattr*/
    0,                                /*tp_setattr*/
    0,                                /*tp_compare*/
    0,                                /*tp_repr*/
    0,                                /*tp_as_number*/
    0,                                /*tp_as_sequence*/
    0,                                /*tp_as_mapping*/
    0,                                /*tp_hash */
    0,                                /*tp_call*/
    0,                                /*tp_str*/
    0,                                /*tp_getattro*/
    0,                                /*tp_setattro*/
    0,                                /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,/*tp_flags*/
    Journalctl__doc__,                /* tp_doc */
    0,                                /* tp_traverse */
    0,                                /* tp_clear */
    0,                                /* tp_richcompare */
    0,                                /* tp_weaklistoffset */
    Journalctl_iter,                  /* tp_iter */
    Journalctl_iternext,              /* tp_iternext */
    Journalctl_methods,               /* tp_methods */
    0,                                /* tp_members */
    Journalctl_getseters,             /* tp_getset */
    0,                                /* tp_base */
    0,                                /* tp_dict */
    0,                                /* tp_descr_get */
    0,                                /* tp_descr_set */
    0,                                /* tp_dictoffset */
    (initproc)Journalctl_init,        /* tp_init */
    0,                                /* tp_alloc */
    Journalctl_new,                   /* tp_new */
};

#if PY_MAJOR_VERSION >= 3
static PyModuleDef pyjournalctl_module = {
    PyModuleDef_HEAD_INIT,
    "pyjournalctl",
    "Module that reads systemd journal similar to journalctl.",
    -1,
    NULL, NULL, NULL, NULL, NULL
};
#endif

PyMODINIT_FUNC
#if PY_MAJOR_VERSION >= 3
PyInit_pyjournalctl(void)
#else
initpyjournalctl(void) 
#endif
{
    PyObject* m;

    PyDateTime_IMPORT;

    if (PyType_Ready(&JournalctlType) < 0)
#if PY_MAJOR_VERSION >= 3
        return NULL;
#else
        return;
#endif

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&pyjournalctl_module);
    if (m == NULL)
        return NULL;
#else
    m = Py_InitModule3("pyjournalctl", NULL,
                   "Module that reads systemd journal similar to journalctl.");
    if (m == NULL)
        return;
#endif

    Py_INCREF(&JournalctlType);
    PyModule_AddObject(m, "Journalctl", (PyObject *)&JournalctlType);
    PyModule_AddStringConstant(m, "__version__", "0.7.0");
    PyModule_AddIntConstant(m, "SD_JOURNAL_NOP", SD_JOURNAL_NOP);
    PyModule_AddIntConstant(m, "SD_JOURNAL_APPEND", SD_JOURNAL_APPEND);
    PyModule_AddIntConstant(m, "SD_JOURNAL_INVALIDATE", SD_JOURNAL_INVALIDATE);
    PyModule_AddIntConstant(m, "SD_JOURNAL_LOCAL_ONLY", SD_JOURNAL_LOCAL_ONLY);
    PyModule_AddIntConstant(m, "SD_JOURNAL_RUNTIME_ONLY", SD_JOURNAL_RUNTIME_ONLY);
    PyModule_AddIntConstant(m, "SD_JOURNAL_SYSTEM_ONLY", SD_JOURNAL_SYSTEM_ONLY);

    /* Message Constants */
    PyModule_AddStringConstant(m, "SD_MESSAGE_JOURNAL_START", SD_ID128_CONST_STR(SD_MESSAGE_JOURNAL_START));
    PyModule_AddStringConstant(m, "SD_MESSAGE_JOURNAL_STOP", SD_ID128_CONST_STR(SD_MESSAGE_JOURNAL_STOP));
    PyModule_AddStringConstant(m, "SD_MESSAGE_JOURNAL_DROPPED", SD_ID128_CONST_STR(SD_MESSAGE_JOURNAL_DROPPED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_COREDUMP", SD_ID128_CONST_STR(SD_MESSAGE_COREDUMP));
#ifdef SD_MESSAGE_JOURNAL_MISSED
    PyModule_AddStringConstant(m, "SD_MESSAGE_JOURNAL_MISSED", SD_ID128_CONST_STR(SD_MESSAGE_JOURNAL_MISSED));
#endif /* SD_MESSAGE_JOURNAL_MISSED */
#ifdef SD_MESSAGE_SESSION_START
    PyModule_AddStringConstant(m, "SD_MESSAGE_SESSION_START", SD_ID128_CONST_STR(SD_MESSAGE_SESSION_START));
    PyModule_AddStringConstant(m, "SD_MESSAGE_SESSION_STOP", SD_ID128_CONST_STR(SD_MESSAGE_SESSION_STOP));
    PyModule_AddStringConstant(m, "SD_MESSAGE_SEAT_START", SD_ID128_CONST_STR(SD_MESSAGE_SEAT_START));
    PyModule_AddStringConstant(m, "SD_MESSAGE_SEAT_STOP", SD_ID128_CONST_STR(SD_MESSAGE_SEAT_STOP));
    PyModule_AddStringConstant(m, "SD_MESSAGE_TIME_CHANGE", SD_ID128_CONST_STR(SD_MESSAGE_TIME_CHANGE));
    PyModule_AddStringConstant(m, "SD_MESSAGE_TIMEZONE_CHANGE", SD_ID128_CONST_STR(SD_MESSAGE_TIMEZONE_CHANGE));
    PyModule_AddStringConstant(m, "SD_MESSAGE_STARTUP_FINISHED", SD_ID128_CONST_STR(SD_MESSAGE_STARTUP_FINISHED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_SLEEP_START", SD_ID128_CONST_STR(SD_MESSAGE_SLEEP_START));
    PyModule_AddStringConstant(m, "SD_MESSAGE_SLEEP_STOP", SD_ID128_CONST_STR(SD_MESSAGE_SLEEP_STOP));
    PyModule_AddStringConstant(m, "SD_MESSAGE_SHUTDOWN", SD_ID128_CONST_STR(SD_MESSAGE_SHUTDOWN));
    PyModule_AddStringConstant(m, "SD_MESSAGE_UNIT_STARTING", SD_ID128_CONST_STR(SD_MESSAGE_UNIT_STARTING));
    PyModule_AddStringConstant(m, "SD_MESSAGE_UNIT_STARTED", SD_ID128_CONST_STR(SD_MESSAGE_UNIT_STARTED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_UNIT_STOPPING", SD_ID128_CONST_STR(SD_MESSAGE_UNIT_STOPPING));
    PyModule_AddStringConstant(m, "SD_MESSAGE_UNIT_STOPPED", SD_ID128_CONST_STR(SD_MESSAGE_UNIT_STOPPED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_UNIT_FAILED", SD_ID128_CONST_STR(SD_MESSAGE_UNIT_FAILED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_UNIT_RELOADING", SD_ID128_CONST_STR(SD_MESSAGE_UNIT_RELOADING));
    PyModule_AddStringConstant(m, "SD_MESSAGE_UNIT_RELOADED", SD_ID128_CONST_STR(SD_MESSAGE_UNIT_RELOADED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_FORWARD_SYSLOG_MISSED", SD_ID128_CONST_STR(SD_MESSAGE_FORWARD_SYSLOG_MISSED));
#endif /* SD_MESSAGE_SESSION_START */
#ifdef SD_MESSAGE_SPAWN_FAILED
    PyModule_AddStringConstant(m, "SD_MESSAGE_SPAWN_FAILED", SD_ID128_CONST_STR(SD_MESSAGE_SPAWN_FAILED));
#endif /* SD_MESSAGE_SPAWN_FAILED */
#ifdef SD_MESSAGE_OVERMOUNTING
    PyModule_AddStringConstant(m, "SD_MESSAGE_OVERMOUNTING", SD_ID128_CONST_STR(SD_MESSAGE_OVERMOUNTING));
#endif /* SD_MESSAGE_OVERMOUNTING */
#ifdef SD_MESSAGE_LID_OPENED
    PyModule_AddStringConstant(m, "SD_MESSAGE_LID_OPENED", SD_ID128_CONST_STR(SD_MESSAGE_LID_OPENED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_LID_CLOSED", SD_ID128_CONST_STR(SD_MESSAGE_LID_CLOSED));
    PyModule_AddStringConstant(m, "SD_MESSAGE_POWER_KEY", SD_ID128_CONST_STR(SD_MESSAGE_POWER_KEY));
    PyModule_AddStringConstant(m, "SD_MESSAGE_SUSPEND_KEY", SD_ID128_CONST_STR(SD_MESSAGE_SUSPEND_KEY));
    PyModule_AddStringConstant(m, "SD_MESSAGE_HIBERNATE_KEY", SD_ID128_CONST_STR(SD_MESSAGE_HIBERNATE_KEY));
#endif /* SD_MESSAGE_LID_OPENED */
    /* End message Constants */
#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}
