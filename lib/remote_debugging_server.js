// Dummy discovery endpoint for the websocket-based debug server.

const http = require('http');
const WebSocketServer = require('ws').Server;
const node_debugger = process.binding('node_debugger');

const TARGET_PATH = "/node";
const VERSION_RESPONSE = {
  "Browser": "node.js/" + process.version,
  "Protocol-Version": "1.1",
  "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/45.0.2446.0 Safari/537.36",
  "WebKit-Version": "537.36 (@198122)"
};
const JSON_RESPONSE = [ {
  "description": "node.js instance",
  "devtoolsFrontendUrl": "https://chrome-devtools-frontend.appspot.com/serve_rev/@198849/inspector.html",
  "faviconUrl": "https://www.google.ru/favicon.ico",
  "id": process.pid,
  "title": process.title,
  "type": "node",
  "url": "http://localhost/node.js",
  "webSocketDebuggerUrl": "ws://" + TARGET_PATH
} ];

function startRemoteDebuggingServer(port) {
  const http_server = http.createServer(function (req, res) {
    res.useChunkedEncodingByDefault = false;
    res.sendData = false;
    var response;
    if (req.url === "/json/version" || req.url === "/json/list" || req.url === "/json") {
      res.writeHead(200, {'Content-Type': 'application/json; charset=UTF-8'});
      var response = req.url === "/json/version" ? VERSION_RESPONSE : JSON_RESPONSE;
      res.end(JSON.stringify(response, null, 2));
    } else {
      res.writeHead(404, {'Content-Type': 'text/plain'});
      res.end("Path not found: " + req.path);
    }
  });
  http_server.listen(port);

  const options = {
    server: http_server,
    path: TARGET_PATH
  };
  const wss = new WebSocketServer(options);

  wss.on('connection', function connection(ws) {
    ws.sendMessageToFrontend = ws.send.bind(ws);
    node_debugger.connectToInspectorBackend(ws);
    ws.on('message', function incoming(message) {
      var parsedMessage = JSON.parse(message);
      if (parsedMessage.method === "Page.getResourceTree") { // pending fix upstream.
        ws.send(JSON.stringify({ id: parsedMessage.id, result: { frameTree: { frame: { id: 1, url: 'url', securityOrigin: 'url', mimeType: 'text/javascript'}, childFrames: [], resources: [] } } }));
      } else {
        node_debugger.dispatchOnInspectorBackend(message);
      }
    });
    ws.on('close', function close() {
      node_debugger.disconnectFromInspectorBackend();
    });
  });
}
startRemoteDebuggingServer(9222);
