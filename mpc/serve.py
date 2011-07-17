import SimpleHTTPServer
import SocketServer

conf = __import__('conf')

# serve static files 
class Handler(SimpleHTTPServer.SimpleHTTPRequestHandler, object):
    def do_GET(self):
        if self.path == '/':
            self.path = 'client.html'
        return super(Handler, self).do_GET()
httpd = SocketServer.TCPServer((conf.http_host, conf.http_port), Handler)
httpd.serve_forever()



