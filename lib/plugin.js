var EventEmitter = require('events').EventEmitter;
var util = require('util');

module.exports = Plugin;

util.inherits(Plugin, EventEmitter);
function Plugin() {
  var self = this;
  self.mpd = null;
  self.is_enabled = true;
  self.checkEnabledMiddleware = function(req, resp, next){
    if (self.is_enabled) {
      next();
    } else {
      resp.writeHead(500, {
        'content-type': 'text/json'
      });
      resp.end(JSON.stringify({
        success: false,
        reason: "DisabledEndpoint"
      }));
    }
  };
  self.whenEnabled = function(middleware){
    return function(req, res, next){
      if (self.is_enabled) {
        middleware(req, res, next);
      } else {
        next();
      }
    };
  };
}
