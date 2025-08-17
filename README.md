# Programmer's Notepad Reloaded

A lightweight and fast text editor for Windows, fork of Simon Steele's [Programmer's Notepad 2](http://www.pnotepad.org/).

Changes from original PN:
- Port to modern Visual Studio, fix some compatibility issues;
- Add GLSL and WGSL syntax highlighting schemes.

## Building

Download [Boost 1.57](https://archives.boost.io/release/1.57.0/source/boost_1_57_0.7z) and extract the source code to `lib/boost/boost-1_57_0`.

Download [WTL 9.0.4140 Final](https://sourceforge.net/projects/wtl/files/WTL%209.0/WTL%209.0.4140%20Final/WTL90_4140_Final.zip/download) and extract the source code to `lib/wtl/WTL90_4140_Final`.

Open and build `pn/pnwtl/pn.sln`.
