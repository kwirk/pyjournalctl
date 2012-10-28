from distutils.core import setup, Extension

setup(name="pyjournalctl",
      version="0.1",
      ext_modules=[Extension("pyjournalctl", ["pyjournalctl.c"],
      libraries=["systemd-journal"])])
