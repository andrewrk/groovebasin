if (!process.env.NODE_ENV) process.env.NODE_ENV = "dev";

var GrooveBasin = require('./groovebasin');
var gb = new GrooveBasin();
gb.on('listening', function() {
  if (process.send) process.send('online');
});
process.on('message', function(message){
  if (message === 'shutdown') process.exit(0);
});
gb.start();
