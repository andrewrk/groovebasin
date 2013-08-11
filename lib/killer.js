var EventEmitter = require('events').EventEmitter;
var util = require('util');

module.exports = Killer;

var POLL_INTERVAL = 100;
var ESCALATE_TIMEOUT = 3000;
var ERROR_TIMEOUT = 2000;

util.inherits(Killer, EventEmitter);
function Killer(pid) {
  EventEmitter.call(this);
  this.pid = pid;
}

Killer.prototype.kill = function() {
  this.interval = setInterval(this.check.bind(this), POLL_INTERVAL);
  this.sig_kill_timeout = setTimeout(this.escalate.bind(this), ESCALATE_TIMEOUT);
  this.sig = "SIGTERM";
};

Killer.prototype.check = function() {
  try {
    process.kill(this.pid, this.sig);
  } catch (err) {
    this.clean();
    if (err.code === 'ESRCH') {
      this.emit('end');
    } else {
      this.emit('error', err);
    }
  }
};

Killer.prototype.clean = function() {
  if (this.interval != null) clearInterval(this.interval);
  this.interval = null;

  if (this.sig_kill_timeout != null) clearTimeout(this.sig_kill_timeout);
  this.sig_kill_timeout = null;

  if (this.error_timeout != null) clearTimeout(this.error_timeout);
  this.error_timeout = null;
};

Killer.prototype.escalate = function() {
  this.sig = "SIGKILL";
  this.error_timeout = setTimeout(this.giveUp.bind(this), ERROR_TIMEOUT);
};

Killer.prototype.giveUp = function() {
  this.clean();
  this.emit('error', new Error("Unable to kill " + this.pid + ": timeout"));
};
