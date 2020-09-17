# Overview

HavoqGT (Highly Asynchronous Visitor Queue Graph Toolkit) is a framework for
expressing asynchronous vertex-centric graph algorithms.  It provides a visitor
interface, where actions are defined at an individual vertex level.
This code was developed at Lawrence Livermore National Laboratory.

Built in C++, the framework provides a runtime for parallel communication and
algorithm termination detection.   V0.1 is an initial release with only MPI support.
All graph data is stored in mmaped files, using Boost.Interprocess and Memory 
Mapped (mmap) I/O.   Large graphs that cannot fit in main-memory may still be
processed using mmap as external memory.  For best results, high speed Flash 
devices are preferred for external memory storage.

For documentation, see http://havoqgt.bitbucket.org

--------------------------------------------------------------------------------
# Getting Started

## Required to Build HavoqGT

- GCC 8.1 or more.
- CMake 2.6 or more.
- Boost C++ Libraries 1.64 or more (build is not required; needs only
  their header files).
- Metall (https://github.com/LLNL/metall) 0.3 or more.

## Build
One can install Boost C++ Libraries and Metall using Spack.
A proper version of Boost C++ Libraries will be installed along with Metall.

An example to build HavoqGT with Spack is:
```bash
spack install metall
spack load metall
cmake ../../ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-std=c++17 -lrt -lstdc++fs -lpthread" \
  -DHAVOQGT_BUILD_TEST=TRUE \
  -DMPIEXEC_NUMPROC_FLAG="-n"
make
make test # option
make install # option
```

Use `CMAKE_CXX_COMPILER=/path/to/g++` and `MPI_CXX_COMPILER=/path/to/mpic++` CMake options to specify a C++ compiler and a MPI compiler, respectively.
To change the install directory, one can use `CMAKE_INSTALL_PREFIX` CMake option.


### Build without Spack

Here are the CMake variables to specify the locations of Boost C++ Libraries and Metall manually.
* `BOOST_ROOT=/path/to/boost`
* `METALL_ROOT=/path/to/metall`

HavoqGT uses header files of the libraries. One does not need to build them.



# About

## Authors

* Roger A Pearce (rpearce at llnl dot gov)
* Keita Iwabuchi (kiwabuchi at llnl dot gov)
* Tahsin A Reza (reza2 at llnl dot gov)

## License

HavoqGT is distributed under the terms of the MIT license.
All new contributions must be made under this license.

See [LICENSE](LICENSE) and [NOTICE](NOTICE) for details.

SPDX-License-Identifier: MIT

## Release

LLNL-CODE-644630