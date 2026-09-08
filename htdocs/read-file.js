// Invoke: js --read /htdocs/hello.js /htdocs/readfile.js
const file = reist.files[0];
const length = file.size();
const prefix = file.readText(64);
file.seek(0);
const bytes = new Uint8Array(file.read(64));
if (!prefix.startsWith("// JS2 example") || bytes[0] !== 47) throw Error("unexpected file");
file.close();
file.close();
console.log("JS_FILE_SHELL_OK", length, bytes.length);
