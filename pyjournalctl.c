/*
pyjournalctl - Python module that reads systemd journald similar to journalctl
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

#include <Python.h>
#include <structmember.h>

typedef struct {
    PyObject_HEAD
    sd_journal *j;
} Journalctl;
static PyTypeObject JournalctlType;

static void
Journalctl_dealloc(Journalctl* self)
{
    sd_journal_close(self->j);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int
Journalctl_init(Journalctl *self, PyObject *args)
{
    int flags=SD_JOURNAL_LOCAL_ONLY;
    if (! PyArg_ParseTuple(args, "|i", &flags))
        return 1;

    int r;
    r = sd_journal_open(&self->j, flags);
    if (r < 0) {
        PyErr_SetString(PyExc_IOError, "Error opening journal");
        return 1;
    }

    return 0;
}

static PyObject *
Journalctl_get_next(Journalctl *self, PyObject *args)
{
    int64_t skip=1LL;
    if (! PyArg_ParseTuple(args, "|L", &skip))
        return NULL;

    int r;
    if (skip == 1LL) {
        r = sd_journal_next(self->j);
    }else if (skip == -1LL) {
        r = sd_journal_previous(self->j);
    }else if (skip > 1LL) {
        r = sd_journal_next_skip(self->j, skip);
    }else if (skip < -1LL) {
        r = sd_journal_previous_skip(self->j, -skip);
    }else{
        PyErr_SetString(PyExc_ValueError, "Skip number must positive/negative integer");
        return NULL;
    }

    if (r < 0) {
        PyErr_SetString(PyExc_IOError, "Error getting next message");
        return NULL;
    }else if ( r == 0) { //EOF
        return PyDict_New();
    }

    PyObject *dict;
    dict = PyDict_New();

    const void *msg;
    size_t msg_len;
    const char *delim_ptr;
    PyObject *key, *value;

    SD_JOURNAL_FOREACH_DATA(self->j, msg, msg_len) {
        delim_ptr = memchr(msg, '=', msg_len);
#if PY_MAJOR_VERSION >=3
        key = PyUnicode_FromStringAndSize(msg, delim_ptr - (const char*) msg);
        value = PyUnicode_FromStringAndSize(delim_ptr + 1, (const char*) msg + msg_len - (delim_ptr + 1));
#else
        key = PyString_FromStringAndSize(msg, delim_ptr - (const char*) msg);
        value = PyString_FromStringAndSize(delim_ptr + 1, (const char*) msg + msg_len - (delim_ptr + 1));
#endif
        PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
    }

    uint64_t realtime;
    if (sd_journal_get_realtime_usec(self->j, &realtime) == 0) {
#if PY_MAJOR_VERSION >=3
        key = PyUnicode_FromString("__REALTIME_TIMESTAMP");
        value = PyUnicode_FromFormat("%llu", realtime);
#else
        key = PyString_FromString("__REALTIME_TIMESTAMP");
        value = PyString_FromFormat("%llu", realtime);
#endif
        PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
    }

    char *bootid;
    sd_id128_t sd_id;
#if PY_MAJOR_VERSION >=3
    PyObject *temp;
    temp = PyUnicode_AsASCIIString(PyDict_GetItemString(dict, "_BOOT_ID"));
    bootid = PyBytes_AsString(temp);
    Py_DECREF(temp);
#else
    bootid = PyString_AsString(PyDict_GetItemString(dict, "_BOOT_ID"));
#endif
    if (sd_id128_from_string(bootid, &sd_id) == 0) {
        uint64_t monotonic;
        if (sd_journal_get_monotonic_usec(self->j, &monotonic, &sd_id) == 0) {
#if PY_MAJOR_VERSION >=3
            key = PyUnicode_FromString("__MONOTONIC_TIMESTAMP");
            value = PyUnicode_FromFormat("%llu", monotonic);
#else
            key = PyString_FromString("__MONOTONIC_TIMESTAMP");
            value = PyString_FromFormat("%llu", monotonic);
#endif
            PyDict_SetItem(dict, key, value);
            Py_DECREF(key);
            Py_DECREF(value);
        }
    }

    char *cursor;
    if (sd_journal_get_cursor(self->j, &cursor) > 0) { //Should return 0...
#if PY_MAJOR_VERSION >=3
        key = PyUnicode_FromString("__CURSOR");
        value = PyUnicode_FromString(cursor);
#else
        key = PyString_FromString("__CURSOR");
        value = PyString_FromString(cursor);
#endif
        PyDict_SetItem(dict, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
    }

    return dict;
}

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

static PyObject *
Journalctl_add_match(Journalctl *self, PyObject *args)
{
    char *match_key, *match_value=NULL;
    int match_key_len, match_value_len;
    if (! PyArg_ParseTuple(args, "s#|s#", &match_key, &match_key_len, &match_value, &match_value_len))
        return NULL;

    char *match;
    int match_len;
    if (match_value) {
        match = malloc(match_key_len + match_value_len + 2);
        match_len = sprintf(match, "%s=%s", match_key, match_value);
    }else{
        match = match_key;
        match_len = match_key_len;
    }

    if (sd_journal_add_match(self->j, match, match_len) != 0) {
        PyErr_SetString(PyExc_IOError, "Error adding match");
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyObject *
Journalctl_add_matches(Journalctl *self, PyObject *args)
{
    PyObject *dict;
    if (! PyArg_ParseTuple(args, "O", &dict))
        return NULL;
    if (!PyDict_Check(dict)) {
        Py_XDECREF(dict);
        PyErr_SetString(PyExc_ValueError, "Argument must be dictionary type");
        return NULL;
    }

    // First must check all are strings
    Py_ssize_t pos=0;
    PyObject *key, *value;
    while (PyDict_Next(dict, &pos, &key, &value)) {
#if PY_MAJOR_VERSION >=3
        if (!(PyUnicode_Check(key) && PyUnicode_Check(value))) {
#else
        if (!(PyString_Check(key) && PyString_Check(value))) {
#endif
            PyErr_SetString(PyExc_ValueError, "Dictionary keys and values must be strings");
            return NULL;
        }
    }
    pos = 0; //Back to start of dictionary
    PyObject *arg;
    while (PyDict_Next(dict, &pos, &key, &value)) {
        arg = Py_BuildValue("OO", key, value);
        Journalctl_add_match(self, arg);
        Py_DECREF(arg);
    }

    Py_DECREF(dict);
    Py_RETURN_NONE;
}

static PyObject *
Journalctl_add_disjunction(Journalctl *self, PyObject *args)
{
    if (sd_journal_add_disjunction(self->j) != 0) {
        PyErr_SetString(PyExc_IOError, "Error adding disjunction");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Journalctl_flush_matches(Journalctl *self, PyObject *args)
{
    //if (sd_journal_flush_matches(self->j) != 0) {
    //    PyErr_SetString(PyExc_IOError, "Error flushing matches");
    //    return NULL;
    //}
    sd_journal_flush_matches(self->j);
    Py_RETURN_NONE;
}

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
        if (sd_journal_seek_head(self->j) !=0 ) {
            PyErr_SetString(PyExc_IOError, "Error seeking to head");
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
        if (sd_journal_seek_tail(self->j) != 0) {
            PyErr_SetString(PyExc_IOError, "Error seeking to tail");
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
        PyErr_SetString(PyExc_IOError, "Invalid value for whence");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Journalctl_seek_realtime(Journalctl *self, PyObject *args)
{
    int64_t time;
    if (! PyArg_ParseTuple(args, "L", &time))
        return NULL;
    if (time < 0LL) {
        PyErr_SetString(PyExc_ValueError, "Time must be positive integer");
        return NULL;
    }

    if (sd_journal_seek_realtime_usec(self->j, time) != 0) {
        PyErr_SetString(PyExc_IOError, "Error seek to time");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Journalctl_seek_monotonic(Journalctl *self, PyObject *args)
{
    int64_t time;
    char *bootid;
    if (! PyArg_ParseTuple(args, "Ls", &time, &bootid))
        return NULL;
    if (time < 0LL) {
        PyErr_SetString(PyExc_ValueError, "Time must be positive integer");
        return NULL;
    }

    sd_id128_t sd_id;
    if (sd_id128_from_string(bootid, &sd_id) == 0) {
        if (sd_journal_seek_monotonic_usec(self->j, sd_id, time) != 0) {
            PyErr_SetString(PyExc_IOError, "Error seek to time");
            return NULL;
        }
    }else{
        PyErr_SetString(PyExc_ValueError, "Invalid bootid");
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
Journalctl_wait(Journalctl *self, PyObject *args, PyObject *keywds)
{
    int64_t timeout=0LL;
    if (! PyArg_ParseTuple(args, "|L", &timeout))
        return NULL;

    int r;
    if ( timeout == 0LL) {
        r = sd_journal_wait(self->j, (uint64_t) -1);
    }else{
        r = sd_journal_wait(self->j, timeout * 1E6);
    }
#if PY_MAJOR_VERSION >=3
    return PyLong_FromLong(r);
#else
    return PyInt_FromLong(r);
#endif
}

static PyObject *
Journalctl_seek_cursor(Journalctl *self, PyObject *args)
{
    const char *cursor;
    if (! PyArg_ParseTuple(args, "s", &cursor))
        return NULL;

    if (sd_journal_seek_cursor(self->j, cursor) != 0) {
        PyErr_SetString(PyExc_IOError, "Error seeking to cursor");
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
static PyObject *
Journalctl_query_unique(Journalctl *self, PyObject *args)
{
    char *query;
    if (! PyArg_ParseTuple(args, "s", &query))
        return NULL;

    if (sd_journal_query_unique(self->j, query) != 0) {
        PyErr_SetString(PyExc_IOError, "Error querying journal");
        return NULL;
    }

    const void *uniq;
    size_t uniq_len;
    const char *delim_ptr;
    PyObject *list, *value;
    list = PyList_New(0);

    SD_JOURNAL_FOREACH_UNIQUE(self->j, uniq, uniq_len) {
        delim_ptr = memchr(uniq, '=', uniq_len);
#if PY_MAJOR_VERSION >=3
        value = PyUnicode_FromStringAndSize(delim_ptr + 1, (const char*) uniq + uniq_len - (delim_ptr + 1));
#else
        value = PyString_FromStringAndSize(delim_ptr + 1, (const char*) uniq + uniq_len - (delim_ptr + 1));
#endif
        PyList_Append(list, value);
        Py_DECREF(value);
    }
    return list;
}
#endif //def SD_JOURNAL_FOREACH_UNIQUE

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
    PyObject *arg;
    for(i = 0; i <= level; i++) {
        sprintf(level_str, "%i", i);
        arg = Py_BuildValue("(ss)", "PRIORITY", level_str);
        Journalctl_add_match(self, arg);
        Py_DECREF(arg);
    }
    Py_RETURN_NONE;
}

static PyMemberDef Journalctl_members[] = {
    {NULL}  /* Sentinel */
};

static PyMethodDef Journalctl_methods[] = {
    {"get_next", (PyCFunction)Journalctl_get_next, METH_VARARGS,
    "Get next message"},
    {"get_previous", (PyCFunction)Journalctl_get_previous, METH_VARARGS,
    "Get previous message"},
    {"add_match", (PyCFunction)Journalctl_add_match, METH_VARARGS,
    "Add an 'and' match filter"},
    {"add_matches", (PyCFunction)Journalctl_add_matches, METH_VARARGS,
    "Adds multiple 'and' match filters"},
    {"add_disjunction", (PyCFunction)Journalctl_add_disjunction, METH_NOARGS,
    "Add an 'or' match filter"},
    {"flush_matches", (PyCFunction)Journalctl_flush_matches, METH_NOARGS,
    "Clear match filter"},
    {"seek", (PyCFunction)Journalctl_seek, METH_VARARGS | METH_KEYWORDS,
    "Seek through journal"},
    {"seek_realtime", (PyCFunction)Journalctl_seek_realtime, METH_VARARGS,
    "Seek to nearest log entry to given time in usecs"},
    {"seek_monotonic", (PyCFunction)Journalctl_seek_monotonic, METH_VARARGS,
    "Seek to nearest log entry to given monotonic time in usecs and given bootid"},
    {"wait", (PyCFunction)Journalctl_wait, METH_VARARGS,
    "Block for number of seconds or until new log entry. 0 seconds waits indefinitely"},
    {"seek_cursor", (PyCFunction)Journalctl_seek_cursor, METH_VARARGS,
    "Seek to log entry for given unique reference cursor"},
#ifdef SD_JOURNAL_FOREACH_UNIQUE
    {"query_unique", (PyCFunction)Journalctl_query_unique, METH_VARARGS,
    "Get unique values for given field name"},
#endif
    {"log_level", (PyCFunction)Journalctl_log_level, METH_VARARGS,
    "Set maximum log level by adding match for PRIORITY"},
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
    "pyjournalctl Journalctl objects",/* tp_doc */
    0,                                /* tp_traverse */
    0,                                /* tp_clear */
    0,                                /* tp_richcompare */
    0,                                /* tp_weaklistoffset */
    Journalctl_iter,                  /* tp_iter */
    Journalctl_iternext,              /* tp_iternext */
    Journalctl_methods,               /* tp_methods */
    Journalctl_members,               /* tp_members */
    0,                                /* tp_getset */
    0,                                /* tp_base */
    0,                                /* tp_dict */
    0,                                /* tp_descr_get */
    0,                                /* tp_descr_set */
    0,                                /* tp_dictoffset */
    (initproc)Journalctl_init,        /* tp_init */
    0,                                /* tp_alloc */
    PyType_GenericNew,                /* tp_new */
};

#if PY_MAJOR_VERSION >= 3
static PyModuleDef pyjournalctl_module = {
    PyModuleDef_HEAD_INIT,
    "pyjournalctl",
    "Module that interfaces with journalctl.",
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
                       "Module that interfaces with journalctl.");
    if (m == NULL)
        return;
#endif

    Py_INCREF(&JournalctlType);
    PyModule_AddObject(m, "Journalctl", (PyObject *)&JournalctlType);
#if PY_MAJOR_VERSION >= 3
    PyModule_AddObject(m, "SD_JOURNAL_NOP", PyLong_FromLong(SD_JOURNAL_NOP));
    PyModule_AddObject(m, "SD_JOURNAL_APPEND", PyLong_FromLong(SD_JOURNAL_APPEND));
    PyModule_AddObject(m, "SD_JOURNAL_INVALIDATE", PyLong_FromLong(SD_JOURNAL_INVALIDATE));
    PyModule_AddObject(m, "SD_JOURNAL_LOCAL_ONLY", PyLong_FromLong(SD_JOURNAL_LOCAL_ONLY));
    PyModule_AddObject(m, "SD_JOURNAL_RUNTIME_ONLY", PyLong_FromLong(SD_JOURNAL_RUNTIME_ONLY));
    PyModule_AddObject(m, "SD_JOURNAL_SYSTEM_ONLY", PyLong_FromLong(SD_JOURNAL_SYSTEM_ONLY));
    return m;
#else
    PyModule_AddObject(m, "SD_JOURNAL_NOP", PyInt_FromLong(SD_JOURNAL_NOP));
    PyModule_AddObject(m, "SD_JOURNAL_APPEND", PyInt_FromLong(SD_JOURNAL_APPEND));
    PyModule_AddObject(m, "SD_JOURNAL_INVALIDATE", PyInt_FromLong(SD_JOURNAL_INVALIDATE));
    PyModule_AddObject(m, "SD_JOURNAL_LOCAL_ONLY", PyInt_FromLong(SD_JOURNAL_LOCAL_ONLY));
    PyModule_AddObject(m, "SD_JOURNAL_RUNTIME_ONLY", PyInt_FromLong(SD_JOURNAL_RUNTIME_ONLY));
    PyModule_AddObject(m, "SD_JOURNAL_SYSTEM_ONLY", PyInt_FromLong(SD_JOURNAL_SYSTEM_ONLY));
#endif
}
