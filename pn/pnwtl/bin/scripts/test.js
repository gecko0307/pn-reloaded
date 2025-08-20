const PN = require("./pnscript");

const pn = new PN(process.argv[2]);

pn.showMessage("Hello from JS!");
pn.insertText("Text inserted by JS!");
