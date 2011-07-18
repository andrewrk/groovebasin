conf = __import__('conf')

def run_http_server():
    "serve static files and downloadable songs"

    import SimpleHTTPServer
    import SocketServer

    class Handler(SimpleHTTPServer.SimpleHTTPRequestHandler, object):
        def do_GET(self):
            if self.path == '/':
                self.path = 'client.html'
            return super(Handler, self).do_GET()
    httpd = SocketServer.TCPServer((conf.http_host, conf.http_port), Handler)
    httpd.serve_forever()

def run_websockets_proxy():
    "connect javascript to mpd directly"

    import struct
    import hashlib
    import socket

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind((conf.ws_host, conf.ws_port))
    sock.listen(5)

    handshake_template = """\
HTTP/1.1 101 Web Socket Protocol Handshake\r\n\
Upgrade: WebSocket\r\n\
Connection: Upgrade\r\n\
Sec-WebSocket-Origin: http://%(http_host)s:%(http_port)i\r\n\
Sec-WebSocket-Location: ws://%(ws_host)s:%(ws_port)i%(ws_path)s\r\n\
Sec-WebSocket-Protocol: proxy\r\n\
\r\n\
%(code_result)s\
"""
    HANDSHAKE, DATA = range(2)

    def run_accept_connection(client, address):
        protocol_state = HANDSHAKE

        data = ''
        while True:
            if protocol_state == HANDSHAKE:
                oldlen = len(data)
                data += client.recv(16)
                newlen = len(data)
                if newlen == oldlen:
                    break
                pos = data.find('\r\n\r\n')
                # make sure we have all the http headers
                if pos == -1:
                    continue
                # make sure we have the 8 bytes for the code
                if len(data) != pos + 4 + 8:
                    continue

                header, in_code = data.split('\r\n\r\n', 1)
                data = ''
                print("<header>" + header + "</header>")
                print("<code>" + in_code + "</code>")

                # parse headers
                headers = {}
                for line in header.split('\r\n'):
                    if line.startswith('GET'):
                        verb, path, protocol = line.split()
                    else:
                        k, v = line.split(': ', 1)
                        headers[k] = v

                # figure out secret code
                def get_num(key):
                    num = ''
                    spc_count = 0
                    for c in key:
                        if c.isdigit():
                            num += c
                        elif c == ' ':
                            spc_count += 1
                    if spc_count == 0:
                        return 0
                    
                    return int(num) / spc_count

                n1 = get_num(headers['Sec-WebSocket-Key1'])
                n2 = get_num(headers['Sec-WebSocket-Key2'])
                dig = hashlib.md5()

                dig.update(struct.pack('>ii', n1, n2))
                dig.update(in_code)

                handshake_data = {
                    'http_host': conf.http_host,
                    'http_port': conf.http_port,
                    'ws_host': conf.ws_host,
                    'ws_port': conf.ws_port,
                    'ws_path': conf.ws_path,
                    'code_result': dig.digest(),
                }
                handshake = handshake_template % handshake_data

                print("<handshake>" + handshake + "</handshake>")
                client.send(handshake)

                protocol_state = DATA

            elif protocol_state == DATA:
                oldlen = len(data)
                data += client.recv(128)
                newlen = len(data)
                if newlen == oldlen:
                    break

                validated = []

                msgs = data.split('\xff')
                data = msgs.pop()

                for msg in msgs:
                    if msg[0] == '\x00':
                        validated.append(msg[1:])

                for v in validated:
                    print(v)
                    client.send('\x00' + v.upper() + '\xff')
        client.close()

    while True:
        client, address = sock.accept()
        
        connection_thread = threading.Thread(target=run_accept_connection, name='ws_%s_%i' % address, args=(client, address))
        connection_thread.daemon = True
        connection_thread.start()

    sock.close()


import threading

http_server = threading.Thread(target=run_http_server, name='http_server')
http_server.daemon = True

ws_proxy = threading.Thread(target=run_websockets_proxy, name='ws_proxy')
ws_proxy.daemon = True

http_server.start()
ws_proxy.start()

import sys
line = sys.stdin.read()
print("closing connections")
