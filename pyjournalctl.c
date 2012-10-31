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
    if (! PyArg_ParseTuple(args, ""))
        return 1;

    int r;
    r = sd_journal_open(&self->j, SD_JOURNAL_LOCAL_ONLY);
    if (r < 0) {
        PyErr_SetString(PyExc_IOError, "Error sending data");
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

    SD_JOURNAL_FOREACH_DATA(self->j, msg, msg_len) {
        delim_ptr = memchr(msg, '=', msg_len);
#if PY_MAJOR_VERSION >=3
        PyDict_SetItem(dict, PyUnicode_FromStringAndSize(msg, delim_ptr - (const char*) msg), PyUnicode_FromStringAndSize(delim_ptr + 1, (const char*) msg + msg_len - (delim_ptr + 1)));
#else
        PyDict_SetItem(dict, PyString_FromStringAndSize(msg, delim_ptr - (const char*) msg), PyString_FromStringAndSize(delim_ptr + 1, (const char*) msg + msg_len - (delim_ptr + 1)));
#endif
    }
    return dict;
}

static PyObject *
Journalctl_get_previous(Journalctl *self, PyObject *args)
{
    int64_t skip=1LL;
    if (! PyArg_ParseTuple(args, "|L", &skip))
        return NULL;

    return Journalctl_get_next(self, Py_BuildValue("(L)", -skip));
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
    while (PyDict_Next(dict, &pos, &key, &value))
        Journalctl_add_match(self, Py_BuildValue("OO", key, value));

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

    if (whence == SEEK_SET){
        if (sd_journal_seek_head(self->j) !=0 ) {
            PyErr_SetString(PyExc_IOError, "Error seeking to head");
            return NULL;
        }
        if (offset > 0LL)
            Journalctl_get_next(self, Py_BuildValue("(L)", offset));
    }else if (whence == SEEK_CUR){
        Journalctl_get_next(self, Py_BuildValue("(L)", offset));
    }else if (whence == SEEK_END){
        if (sd_journal_seek_tail(self->j) != 0) {
            PyErr_SetString(PyExc_IOError, "Error seeking to tail");
            return NULL;
        }
        Journalctl_get_next(self, Py_BuildValue("(L)", -1LL));
        if (offset < 0LL)
            Journalctl_get_next(self, Py_BuildValue("(L)", offset));
    }else{
        PyErr_SetString(PyExc_IOError, "Invalid value for whence");
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
Journalctl_get_cursor(Journalctl *self, PyObject *args)
{
    char *cursor;
    if (sd_journal_get_cursor(self->j, &cursor) < 0) { //Should return 0...
        PyErr_SetString(PyExc_IOError, "Error getting cursor");
        return NULL;
    }

#if PY_MAJOR_VERSION >=3
    return PyUnicode_FromString(cursor);
#else
    return PyString_FromString(cursor);
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
    PyObject *dict;
    Py_ssize_t dict_size;

    dict = Journalctl_get_next(iter, Py_BuildValue("()"));
    dict_size = PyDict_Size(dict);
    if ((int64_t) dict_size > 0LL) {
        return dict;
    }else{
        Py_DECREF(dict);
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }
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
    {"wait", (PyCFunction)Journalctl_wait, METH_VARARGS,
    "Block for number of seconds or until new log entry. 0 seconds waits indefinitely"},
    {"get_cursor", (PyCFunction)Journalctl_get_cursor, METH_NOARGS,
    "Get unique reference cursor for current entry"},
    {"seek_cursor", (PyCFunction)Journalctl_seek_cursor, METH_VARARGS,
    "Seek to log entry for given unique reference cursor"},
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
    return m;
#else
    PyModule_AddObject(m, "SD_JOURNAL_NOP", PyInt_FromLong(SD_JOURNAL_NOP));
    PyModule_AddObject(m, "SD_JOURNAL_APPEND", PyInt_FromLong(SD_JOURNAL_APPEND));
    PyModule_AddObject(m, "SD_JOURNAL_INVALIDATE", PyInt_FromLong(SD_JOURNAL_INVALIDATE));
#endif
}
