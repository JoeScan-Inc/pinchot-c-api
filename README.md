# JoeScan Pinchot C API
The JoeScan Pinchot C API is the C/C++ language interface to JoeScan JS-50
scan heads. This API allows users to develop software to run on a desktop
computer and control scan heads that are connected to the same network.

If writing software to control classic JoeScan products such as the JS-20 or
JS-25, please download the supporting software
[here](http://help.joescan.com/display/ds/downloads).

Note that this is the open source release of the Joescan Pinchot API and will
need to be manually built and compiled by the end user. The official
precompiled version of this API can be found
[here](http://api.joescan.com/release).

## Examples
Example use of this API can be found in the `examples` directory. These
examples are ordered numerically and are meant to provide instruction as to how
the API is to be used. For new users, it is recommended to begin with
`00-configure-and-connect` and work upwards to the remaining examples.

## Build Dependencies
In order to build the API and software examples in Windows 10, the following
tools should be installed.
- CMake
- Python 3
- Visual Studio 2019/2017/2015

For Linux, the following tools must be installed in order to build the API and
software.
- CMake
- Make
- Python 3
- g++

## Build Instructions
To build the API and/or software examples, use CMake to target one of the
`CMakeLists.txt` files located in the directory of the component that is
desired to be built. Once CMake generates the system specific build files, use
either Visual Studio in Windows or Make/g++ in Linux to build the software.

## Support
For direct support for the JoeScan Pinchot API, please reach out to your
JoeScan company representative and we will provide assistance as soon as
possible. The Github page for this project is also monitored by developers
within JoeScan and can be used to post questions or issues.
