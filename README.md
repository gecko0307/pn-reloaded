# PN Reloaded

A lightweight and fast text editor for Windows, fork of Simon Steele's [Programmer's Notepad 2](http://www.pnotepad.org/).

Changes from original PN:
- Port to modern Visual Studio, fix some compatibility issues;
- Add WTL to the repo;
- New icon;
- Files with unknown encoding are now loaded as UTF-8;
- Autodetect indentation style (tabs/spaces);
- *.exe files are now executed by double clicking in the file browser and project view;
- Add Haskell, GLSL, HLSL, WGSL syntax highlighting schemes;
- Support for JSON files (treated as JavaScript files for syntax highlighting);
- Add Text Clip Creator and Project Template Editor to the distribution;
- Add files necessary to build CHM help file using HTML Help Workshop;
- PNScript - a Node.js scripting extension.

## Building

Download [Boost 1.57](https://archives.boost.io/release/1.57.0/source/boost_1_57_0.7z) and extract the source code to `lib/boost/boost-1_57_0`.

Open and build `pn/pnwtl/pn.sln`.

## Scripting via Node.js
PN Reloaded features pnscript.dll, an extension that makes possible to run JavaScript inside the editor. This requires Node.js.

PNScript looks for *.js files in `scripts` folder. Each script is registered in Extensions menu.

Scripts work as text filters. PNScript saves the current document to a temporary file and sends its path as an argument, as well as the output file path. The output is then fed to the newly opened document.

The following example implements an uppercase filter:

```js
const fs = require("fs");
const inputFile = process.argv[2];
const outputFile = process.argv[3];

const input = fs.readFileSync(inputFile, "utf8");
fs.writeFileSync(outputFile, input.toUpperCase());
```

It is possible to add custom NPM packages, simply go to the `scripts` folder and run `npm install <package>` there.
