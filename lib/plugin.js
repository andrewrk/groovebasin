var EventEmitter;
EventEmitter = require('events').EventEmitter;
module.exports = (function(superclass){
  exports.displayName = 'exports';
  var prototype = extend$(exports, superclass).prototype, constructor = exports;
  function exports(){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    this$.mpd = null;
    this$.is_enabled = true;
    this$.checkEnabledMiddleware = function(req, resp, next){
      if (this$.is_enabled) {
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
    this$.whenEnabled = function(middleware){
      return function(req, res, next){
        if (this$.is_enabled) {
          middleware(req, res, next);
        } else {
          next();
        }
      };
    };
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  return exports;
}(EventEmitter));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}