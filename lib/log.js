var levels = {
  Fatal: 0,
  Error: 1,
  Info:  2,
  Warn:  3,
  Debug: 4,
};

exports.levels = levels;
exports.level = levels.Info;
exports.log = log;
exports.fatal = makeLogFn(levels.Fatal);
exports.error = makeLogFn(levels.Error);
exports.info = makeLogFn(levels.Info);
exports.warn = makeLogFn(levels.Warn);
exports.debug = makeLogFn(levels.Debug);


function makeLogFn(level) {
  return log.bind(log, level);
}

function log(level) {
  if (level > exports.level) return;
  console.error.apply(console, Array.prototype.slice.call(arguments, 1));
}
