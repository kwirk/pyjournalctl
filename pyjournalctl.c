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
    self->ob_type->tp_free((PyObject*)self);
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
        PyDict_SetItem(dict, PyString_FromStringAndSize(msg, delim_ptr - (const char*) msg), PyString_FromStringAndSize(delim_ptr + 1, (const char*) msg + msg_len - (delim_ptr + 1)));
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
    char *match;
    int match_len;
    if (! PyArg_ParseTuple(args, "s#", &match, &match_len))
        return NULL;

    if (sd_journal_add_match(self->j, match, match_len) != 0) {
        PyErr_SetString(PyExc_IOError, "Error adding match");
        return NULL;
    }

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
    int whence=SEEK_CUR;
    static char *kwlist[] = {"offset", "whence", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, keywds, "L|i", kwlist,
                                      &offset, &whence))
        return NULL;

    if (whence == SEEK_CUR){
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
        Journalctl_get_previous(self, Py_BuildValue("(L)", offset+1));
    }else{
        PyErr_SetString(PyExc_IOError, "Invalid value for whence");
        return NULL;
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
    {"add_disjunction", (PyCFunction)Journalctl_add_disjunction, METH_NOARGS,
    "Add an 'or' match filter"},
    {"flush_matches", (PyCFunction)Journalctl_flush_matches, METH_NOARGS,
    "Clear match filter"},
    {"seek", (PyCFunction)Journalctl_seek, METH_VARARGS | METH_KEYWORDS,
    "Seek through journal"},
    {NULL}  /* Sentinel */
};

static PyTypeObject JournalctlType = {
    PyObject_HEAD_INIT(NULL)
    0,                                /*ob_size*/
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
    0,                                /* tp_iter */
    0,                                /* tp_iternext */
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

PyMODINIT_FUNC
initpyjournalctl(void) 
{
    PyObject* m;

    if (PyType_Ready(&JournalctlType) < 0)
        return;

    m = Py_InitModule3("pyjournalctl", NULL,
                       "Module that interfaces with journalctl.");
    if (m == NULL)
        return;

    Py_INCREF(&JournalctlType);
    PyModule_AddObject(m, "Journalctl", (PyObject *)&JournalctlType);
}
