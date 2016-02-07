// Dummy discovery endpoint for the websocket-based debug server.

function startRemoteDebuggingServer(port) {
  var http = require('http');
  var target_path = "/node";
  var http_server = http.createServer(function (req, res) {
    var response;
    if (req.url === "/json/version") {
      response = {
        "Browser": "node.js/" + process.version,
        "Protocol-Version": "1.1",
        "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/45.0.2446.0 Safari/537.36",
        "WebKit-Version": "537.36 (@198122)"
      };
    } else if (req.url === "/json/list" || req.url === "/json") {
      response = [ {
         "description": "node.js instance",
         "devtoolsFrontendUrl": "https://chrome-devtools-frontend.appspot.com/serve_rev/@198849/inspector.html",
         "faviconUrl": "https://www.google.ru/favicon.ico",
         "id": process.pid,
         "title": process.title,
         "type": "node",
         "url": "http://localhost/node.js",
         "webSocketDebuggerUrl": "ws://" + target_path
      } ];
    }
    res.useChunkedEncodingByDefault = false;
    res.sendData = false;
    if (response) {
      res.writeHead(200, {'Content-Type': 'application/json; charset=UTF-8'});
      res.end(JSON.stringify(response, null, 2));
    } else {
      res.writeHead(200, {'Content-Type': 'text/plain'});
      res.end("Path not found: " + req.path);
    }
  });
  http_server.listen(port);

  var WebSocketServer = require('ws').Server;
  var options = {
    server: http_server,
    path: target_path
  };
  var wss = new WebSocketServer(options);
  var node_debugger = process.binding('node_debugger');

  wss.on('connection', function connection(ws) {
    ws.sendMessageToFrontend = function(message) {
      ws.send(message);
    }
    node_debugger.connectToInspectorBackend(ws);
    ws.on('message', function incoming(message) {
      var parsedMessage = JSON.parse(message);
      if (parsedMessage.method === "Page.getResourceTree") { // pending fix upstream.
        ws.send(JSON.stringify({ id: parsedMessage.id, result: { frameTree: { frame: { id: 1, url: 'url', securityOrigin: 'url', mimeType: 'text/javascript'}, childFrames: [], resources: [] } } }));
        return;
      }
      node_debugger.dispatchOnInspectorBackend(message);
    });
    ws.on('close', function close() {
      node_debugger.disconnectFromInspectorBackend();
    });
  });
}
startRemoteDebuggingServer(9222);
