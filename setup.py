from distutils.core import setup, Extension

setup(name="pyjournalctl",
      description="A module that reads systemd journal similar to journalctl",
      long_description=open("README.rst").read(),
      version="0.6.1",
      ext_modules=[Extension("pyjournalctl", ["pyjournalctl.c"],
                   libraries=["systemd-journal", "systemd-id128"])],
      author="Steven Hiscocks",
      author_email="steven@hiscocks.me.uk",
      url="https://github.com/kwirk/pyjournalctl",
      license="GNU Lesser General Public License (LGPL), Version 2",
      keywords="systemd journald journal sd-journal systemd-journal",
      classifiers=[
          "Development Status :: 5 - Production/Stable",
          "Intended Audience :: Developers",
          "Intended Audience :: System Administrators",
          "License :: OSI Approved :: GNU Lesser General Public License v2 or later (LGPLv2+)",
          "Operating System :: POSIX :: Linux",
          "Programming Language :: C",
          "Programming Language :: Python :: 2.7",
          "Programming Language :: Python :: 3",
          "Programming Language :: Python :: Implementation :: CPython",
          "Topic :: Software Development :: Libraries :: Python Modules",
          "Topic :: System :: Logging",
      ]
     )
