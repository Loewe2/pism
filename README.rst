Profiling fork of PISM, a Parallel Ice Sheet Model
================================

The Parallel Ice Sheet Model is an open source, parallel, high-resolution ice sheet model:

- hierarchy of available stress balances
- marine ice sheet physics, dynamic calving fronts
- polythermal, enthalpy-based conservation of energy scheme
- extensible coupling to atmospheric and ocean models
- verification and validation tools
- `documentation <pism-docs_>`_ for users and developers
- uses MPI_ and PETSc_ for parallel simulations
- reads and writes `CF-compliant <cf_>`_  NetCDF_ files

PISM is jointly developed at the `University of Alaska, Fairbanks (UAF) <uaf_>`_ and the
`Potsdam Institute for Climate Impact Research (PIK) <pik_>`_. UAF developers are based in
the `Glaciers Group <glaciers_>`_ at the `Geophysical Institute <gi_>`_.

Please see ``ACKNOWLEDGE.rst`` and ``doc/funding.csv`` for a list of grants supporting
PISM development.

Homepage
--------

    http://www.pism-docs.org/

Download and Install with `Spack <spack_>`_
------------------------------
1. Download

- ``git clone git@github.com:Loewe2/pism.git``

2. Setup dependencies an install script

- ``cd`` to build directory 
- ``spack setup "pism@local ^openmpi +pmi schedulers=auto"``

  - for more options see ``spack info pism``

3. Generate MAKEFILE

- ``./spconfig.py PATH_TO_GIT_REPO``

4. Build

- ``make install``

5. More dependencies

- ``spack install nco py-numpy py-netcdf4``
- ``spack load nco py-numpy py-netcdf4``

Install with existing dependencies (only in SGS Environment)
----------------------------------

1. Download

- ``git clone git@github.com:Loewe2/pism.git`

2. Source Spack and load environment

- ``. /import/sgs.local/scratch/vancraar/spack/share/spack/setup-env.sh``
- ``spack load nco py-numpy py-netcdf4``

4. Generate MAKEFILE

- ``cd`` to build directory 
- ``/import/sgs.local/scratch/vancraar/pism/spconfig.py PATH_TO_GIT_REPO``

5. Build

- ``make install``





Also see the `Installing PISM <pism-installation_>`_ on ``pism-docs.org``!

Support
-------

Please e-mail `uaf-pism@alaska.edu <uaf-pism_>`_ with questions about PISM.

You can also join the PISM workspace on `Slack <Slack-PISM_>`_.

Contributing
------------

Want to contribute? Great! See `Contributing to PISM <pism-contributing_>`_.

.. URLs

.. |cipism| image:: https://circleci.com/gh/pism/pism/tree/master.svg?style=svg
.. _cipism: https://circleci.com/gh/pism/pism/tree/master
.. _uaf: http://www.uaf.edu/
.. _pik: http://www.pik-potsdam.de/
.. _pism-docs: http://www.pism-docs.org/
.. _pism-stable: http://www.pism-docs.org/wiki/doku.php?id=stable_version
.. _pism-contributing: http://pism-docs.org/sphinx/contributing/
.. _pism-installation: http://pism-docs.org/sphinx/installation/
.. _mpi: http://www.mcs.anl.gov/research/projects/mpi/
.. _petsc: http://www.mcs.anl.gov/petsc/
.. _cf: http://cf-pcmdi.llnl.gov/
.. _netcdf: http://www.unidata.ucar.edu/software/netcdf/
.. _glaciers: http://www.gi.alaska.edu/snowice/glaciers/
.. _gi: http://www.gi.alaska.edu
.. _NASA-MAP: http://map.nasa.gov/
.. _NASA-Cryosphere: http://ice.nasa.gov/
.. _NSF-Polar: https://nsf.gov/geo/plr/about.jsp
.. _Slack-PISM: https://join.slack.com/t/uaf-pism/shared_invite/enQtODc3Njc1ODg0ODM5LThmOTEyNjEwN2I3ZTU4YTc5OGFhNGMzOWQ1ZmUzMWUwZDAyMzRlMzBhZDg1NDY5MmQ1YWFjNDU4MDZiNTk3YmE
.. _uaf-pism: mailto:uaf-pism@alaska.edu
.. _spack: https://github.com/spack/spack

..
   Local Variables:
   fill-column: 90
   End:
