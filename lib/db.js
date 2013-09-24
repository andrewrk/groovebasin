var levelup = require('level');

module.exports = function (dbPath) {
  dbPath = dbPath || "./groovebasin.db";
  return levelup(dbPath);
};
