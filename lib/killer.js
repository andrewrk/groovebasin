var POLL_INTERVAL, ESCALATE_TIMEOUT, ERROR_TIMEOUT, Killer;
POLL_INTERVAL = 100;
ESCALATE_TIMEOUT = 3000;
ERROR_TIMEOUT = 2000;
module.exports = Killer = (function(superclass){
  Killer.displayName = 'Killer';
  var prototype = extend$(Killer, superclass).prototype, constructor = Killer;
  function Killer(pid){
    var this$ = this instanceof ctor$ ? this : new ctor$;
    this$.pid = pid;
    return this$;
  } function ctor$(){} ctor$.prototype = prototype;
  prototype.kill = function(){
    this.interval = setInterval(bind$(this, 'check'), POLL_INTERVAL);
    this.sig_kill_timeout = setTimeout(bind$(this, 'escalate'), ESCALATE_TIMEOUT);
    this.sig = "SIGTERM";
  };
  prototype.check = function(){
    var e;
    try {
      process.kill(this.pid, this.sig);
    } catch (e$) {
      e = e$;
      this.clean();
      if (e.code === 'ESRCH') {
        this.emit('end');
      } else {
        this.emit('error', e);
      }
    }
  };
  prototype.clean = function(){
    if (this.interval != null) {
      clearInterval(this.interval);
    }
    this.interval = null;
    if (this.sig_kill_timeout != null) {
      clearTimeout(this.sig_kill_timeout);
    }
    this.sig_kill_timeout = null;
    if (this.error_timeout != null) {
      clearTimeout(this.error_timeout);
    }
    this.error_timeout = null;
  };
  prototype.escalate = function(){
    this.sig = "SIGKILL";
    this.error_timeout = setTimeout(bind$(this, 'giveUp'), ERROR_TIMEOUT);
  };
  prototype.giveUp = function(){
    this.clean();
    this.emit('error', new Error("Unable to kill " + this.pid + ": timeout"));
  };
  return Killer;
}(require('events').EventEmitter));
function extend$(sub, sup){
  function fun(){} fun.prototype = (sub.superclass = sup).prototype;
  (sub.prototype = new fun).constructor = sub;
  if (typeof sup.extended == 'function') sup.extended(sub);
  return sub;
}
function bind$(obj, key){
  return function(){ return obj[key].apply(obj, arguments) };
}