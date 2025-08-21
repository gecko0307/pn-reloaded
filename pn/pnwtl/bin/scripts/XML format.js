const fs = require("fs");
const xmlFormat = require("xml-formatter");

const inputFile = process.argv[2];
const outputFile = process.argv[3];

const input = fs.readFileSync(inputFile, "utf8");
fs.writeFileSync(outputFile, xmlFormat(input));
