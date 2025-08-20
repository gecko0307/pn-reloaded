const net = require("net");

class PN
{
    constructor(pipeName)
    {
        this.pipeName = pipeName;
        this.client = net.createConnection(pipeName);

        this.client.on("error", (err) => {
            console.error("Pipe error:", err);
        });

        this.client.on("data", (data) => {
            console.log("Received from PN:", data.toString());
        });

        this.client.on("close", () => {
            console.log("Connection to PN closed.");
        });
    }

    sendCommand(command)
    {
        try
        {
            const json = JSON.stringify(command);
            this.client.write(json + "\n");
        }
        catch(e)
        {
            console.error("Failed to send command:", e);
        }
    }

    // Helper methods
    showMessage(message)
    {
        this.sendCommand({ action: "showMessage", message });
    }

    openDocument(path)
    {
        this.sendCommand({ action: "openDocument", path });
    }

    insertText(text)
    {
        this.sendCommand({ action: "insertText", text });
    }
}

module.exports = PN;
