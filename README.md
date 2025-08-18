# Programmer's Notepad Reloaded

A lightweight and fast text editor for Windows, fork of Simon Steele's [Programmer's Notepad 2](http://www.pnotepad.org/).

Changes from original PN:
- Port to modern Visual Studio, fix some compatibility issues;
- Add WTL to the repo;
- *.exe files are now executed by double clicking in the file browser and project view;
- Add GLSL and WGSL syntax highlighting schemes;
- Support for JSON files (treated as JavaScript files for syntax highlighting);
- Add files necessary to build CHM help file using HTML Help Workshop.

## Building

Download [Boost 1.57](https://archives.boost.io/release/1.57.0/source/boost_1_57_0.7z) and extract the source code to `lib/boost/boost-1_57_0`.

Open and build `pn/pnwtl/pn.sln`.
