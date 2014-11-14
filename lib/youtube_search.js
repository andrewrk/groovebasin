var google = require('googleapis');
var youtube = google.youtube('v3');

module.exports = youtubeSearch;

function youtubeSearch(name, apiKey, cb) {
  var query = {
    auth: apiKey,
    part: "id",
    q: name,
    safeSearch: "none",
    type: "video",
    videoDefinition: "high",
    maxResults: 1,
  };
  youtube.search.list(query, function(googleErr, response) {
    if (googleErr) {
      var err = new Error(googleErr.message);
      err.code = googleErr.code;
      err.domain = googleErr.domain;
      err.reason = googleErr.reason;
      cb(err);
      return;
    }
    if (!response) return cb(new Error("invalid response"));
    if (!response.items) return cb(new Error("invalid response"));
    if (response.items.length !== 1) return cb(new Error("no results found"));
    var item = response.items[0];
    if (!item) return cb(new Error("invalid response"));
    if (!item.id) return cb(new Error("invalid response"));
    if (!item.id.videoId) return cb(new Error("invalid response"));

    var fullUrl = "https://youtube.com/watch?v=" + item.id.videoId;
    cb(null, fullUrl);
  });
}
