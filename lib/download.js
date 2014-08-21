var http = require('http');
var https = require('https');
var url = require('url');

exports.download = download;

var whichHttpLib = {
  'http:': http,
  'https:': https,
};

function download(urlString, cb) {
  var parsedUrl = url.parse(urlString);
  var httpLib = whichHttpLib[parsedUrl.protocol];
  if (!httpLib) return cb(new Error("Invalid URL"));

  parsedUrl.agent = false;
  parsedUrl.rejectUnauthorized = false;
  httpLib.get(parsedUrl, function(res) {
    cb(null, res);
  }).on('error', function(err) {
    cb(err, null);
  });
}
