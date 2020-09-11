// https://github.com/andrewrk/node-music-library-index

var removeDiacritics = require('removediacritics');

MusicLibraryIndex.defaultPrefixesToStrip = [
  /^\s*the\s+/,
  /^\s*a\s+/,
  /^\s*an\s+/,
];
MusicLibraryIndex.defaultVariousArtistsKey = "VariousArtists";
MusicLibraryIndex.defaultVariousArtistsName = "Various Artists";
MusicLibraryIndex.defaultSearchFields = [
  'artistName',
  'albumArtistName',
  'albumName',
  'name',
];

function MusicLibraryIndex(options) {
  options = options || {};
  this.searchFields = options.searchFields || MusicLibraryIndex.defaultSearchFields;
  this.variousArtistsKey = options.variousArtistsKey || MusicLibraryIndex.defaultVariousArtistsKey;
  this.variousArtistsName = options.variousArtistsName || MusicLibraryIndex.defaultVariousArtistsName;
  this.prefixesToStrip = options.prefixesToStrip || MusicLibraryIndex.defaultPrefixesToStrip;

  this.artistComparator = this.artistComparator.bind(this);
  this.albumComparator = this.albumComparator.bind(this);
  this.trackComparator = this.trackComparator.bind(this);
  this.labelComparator = this.labelComparator.bind(this);
  this.clearTracks();
  this.clearLabels();
}

MusicLibraryIndex.prototype.stripPrefixes = function(str) {
  for (var i = 0; i < this.prefixesToStrip.length; i += 1) {
    var regex = this.prefixesToStrip[i];
    str = str.replace(regex, '');
    break;
  }
  return str;
};

MusicLibraryIndex.prototype.sortableTitle = function(title) {
  return this.stripPrefixes(formatSearchable(title));
};

MusicLibraryIndex.prototype.titleCompare = function(a, b) {
  var _a = this.sortableTitle(a);
  var _b = this.sortableTitle(b);
  if (_a < _b) {
    return -1;
  } else if (_a > _b) {
    return 1;
  } else {
    if (a < b) {
      return -1;
    } else if (a > b) {
      return 1;
    } else {
      return 0;
    }
  }
};

MusicLibraryIndex.prototype.trackComparator = function(a, b) {
  if (a.disc < b.disc) {
    return -1;
  } else if (a.disc > b.disc) {
    return 1;
  } else if (a.track < b.track) {
    return -1;
  } else if (a.track > b.track) {
    return 1;
  } else {
    return this.titleCompare(a.name, b.name);
  }
}

MusicLibraryIndex.prototype.albumComparator = function(a, b) {
  if (a.year < b.year) {
    return -1;
  } else if (a.year > b.year) {
    return 1;
  } else {
    return this.titleCompare(a.name, b.name);
  }
}

MusicLibraryIndex.prototype.artistComparator = function(a, b) {
  return this.titleCompare(a.name, b.name);
}

MusicLibraryIndex.prototype.labelComparator = function(a, b) {
  return this.titleCompare(a.name, b.name);
}

MusicLibraryIndex.prototype.getAlbumKey = function(track) {
  var artistName = track.albumArtistName ||
    (track.compilation ? this.variousArtistsName : track.artistName);
  return formatSearchable(track.albumName + "\n" + artistName);
};

MusicLibraryIndex.prototype.getArtistKey = function(artistName) {
  return formatSearchable(artistName);
};

MusicLibraryIndex.prototype.clearTracks = function() {
  this.trackTable = {};
  this.artistTable = {};
  this.artistList = [];
  this.albumTable = {};
  this.albumList = [];
  this.dirtyTracks = false;
};

MusicLibraryIndex.prototype.clearLabels = function() {
  this.labelTable = {};
  this.labelList = [];
  this.dirtyLabels = false;
};

MusicLibraryIndex.prototype.rebuildAlbumTable = function() {
  // builds everything from trackTable
  this.artistTable = {};
  this.artistList = [];
  this.albumTable = {};
  this.albumList = [];
  var thisAlbumList = this.albumList;
  for (var trackKey in this.trackTable) {
    var track = this.trackTable[trackKey];
    this.trackTable[track.key] = track;

    var searchTags = "";
    for (var i = 0; i < this.searchFields.length; i += 1) {
      searchTags += track[this.searchFields[i]] + "\n";
    }
    track.exactSearchTags = searchTags;
    track.fuzzySearchTags = formatSearchable(searchTags);

    if (track.albumArtistName === this.variousArtistsName) {
      track.albumArtistName = "";
      track.compilation = true;
    }
    track.albumArtistName = track.albumArtistName || "";

    var albumKey = this.getAlbumKey(track);
    var album = getOrCreate(albumKey, this.albumTable, createAlbum);
    track.album = album;
    album.trackList.push(track);
    if (album.year == null) {
      album.year = track.year;
    }
  }

  function createAlbum() {
    var album = {
      name: track.albumName,
      year: track.year,
      trackList: [],
      key: albumKey,
    };
    thisAlbumList.push(album);
    return album;
  }
};

MusicLibraryIndex.prototype.rebuildTracks = function() {
  if (!this.dirtyTracks) return;
  this.rebuildAlbumTable();
  this.albumList.sort(this.albumComparator);

  var albumArtistName, artistKey, artist;
  var albumKey, track, album;
  var i;
  for (albumKey in this.albumTable) {
    album = this.albumTable[albumKey];
    var albumArtistSet = {};
    album.trackList.sort(this.trackComparator);
    albumArtistName = "";
    var isCompilation = false;
    for (i = 0; i < album.trackList.length; i += 1) {
      track = album.trackList[i];
      track.index = i;
      if (track.albumArtistName) {
        albumArtistName = track.albumArtistName;
        albumArtistSet[this.getArtistKey(albumArtistName)] = true;
      }
      if (!albumArtistName) albumArtistName = track.artistName;
      albumArtistSet[this.getArtistKey(albumArtistName)] = true;
      isCompilation = isCompilation || track.compilation;
    }
    if (isCompilation || moreThanOneKey(albumArtistSet)) {
      albumArtistName = this.variousArtistsName;
      artistKey = this.variousArtistsKey;
      for (i = 0; i < album.trackList.length; i += 1) {
        track = album.trackList[i];
        track.compilation = true;
      }
    } else {
      artistKey = this.getArtistKey(albumArtistName);
    }
    artist = getOrCreate(artistKey, this.artistTable, createArtist);
    album.artist = artist;
    artist.albumList.push(album);
  }

  this.artistList = [];
  var variousArtist = null;
  for (artistKey in this.artistTable) {
    artist = this.artistTable[artistKey];
    artist.albumList.sort(this.albumComparator);
    for (i = 0; i < artist.albumList.length; i += 1) {
      album = artist.albumList[i];
      album.index = i;
    }
    if (artist.key === this.variousArtistsKey) {
      variousArtist = artist;
    } else {
      this.artistList.push(artist);
    }
  }
  this.artistList.sort(this.artistComparator);
  if (variousArtist) {
    this.artistList.unshift(variousArtist);
  }
  for (i = 0; i < this.artistList.length; i += 1) {
    artist = this.artistList[i];
    artist.index = i;
  }

  this.dirtyTracks = false;

  function createArtist() {
    return {
      name: albumArtistName,
      albumList: [],
      key: artistKey,
    };
  }
}

MusicLibraryIndex.prototype.rebuildLabels = function() {
  if (!this.dirtyLabels) return;

  this.labelList = [];
  for (var id in this.labelTable) {
    var label = this.labelTable[id];
    this.labelList.push(label);
  }

  this.labelList.sort(this.labelComparator);
  this.labelList.forEach(function(label, index) {
    label.index = index;
  });

  this.dirtyLabels = false;
}

MusicLibraryIndex.prototype.addTrack = function(track) {
  this.trackTable[track.key] = track;
  this.dirtyTracks = true;
}

MusicLibraryIndex.prototype.removeTrack = function(key) {
  delete this.trackTable[key];
  this.dirtyTracks = true;
}

MusicLibraryIndex.prototype.addLabel = function(label) {
  this.labelTable[label.id] = label;
  this.dirtyLabels = true;
}

MusicLibraryIndex.prototype.removeLabel = function(id) {
  delete this.labelTable[id];
  this.dirtyLabels = true;
}

MusicLibraryIndex.prototype.search = function(query) {
  var searchResults = new MusicLibraryIndex({
    searchFields: this.searchFields,
    variousArtistsKey: this.variousArtistsKey,
    variousArtistsName: this.variousArtistsName,
    prefixesToStrip: this.prefixesToStrip,
  });

  var matcher = this.parseQuery(query);

  var track;
  for (var trackKey in this.trackTable) {
    track = this.trackTable[trackKey];
    if (matcher(track)) {
      searchResults.trackTable[track.key] = track;
    }
  }
  searchResults.dirtyTracks = true;
  searchResults.rebuildTracks();

  return searchResults;

};

var tokenizerRegex = new RegExp(
  '( +)'                        +'|'+ // 1: whitespace between terms (not in quotes)
  '(\\()'                       +'|'+ // 2: open parenthesis at the start of a term
  '(\\))'                       +'|'+ // 3: end parenthesis
  '(not:)'                      +'|'+ // 4: not: prefix
  '(or:\\()'                    +'|'+ // 5: or: prefix
  '(label:)'                    +'|'+ // 6: label: prefix
  '("(?:[^"\\\\]|\\\\.)*"\\)*)' +'|'+ // 7: quoted thing. can end with parentheses
  '([^ ]+)',                          // 8: normal word. can end with parentheses
  "g");
var WHITESPACE = 1;
var OPEN_PARENTHESIS = 2;
var CLOSE_PARENTHESIS = 3;
var NOT = 4;
var OR = 5;
var LABEL = 6;
var QUOTED_THING = 7;
var NORMAL_WORD = 8;
MusicLibraryIndex.prototype.parseQuery = function(query) {
  var self = this;
  return parse(query);

  function parse(query) {
    var tokens = tokenizeQuery(query);
    var tokenIndex = 0;
    return parseList(makeAndMatcher, null);

    function parseList(makeMatcher, waitForTokenType) {
      var matchers = [];
      var justSawWhitespace = true;
      while (tokenIndex < tokens.length) {
        var token = tokens[tokenIndex++];
        switch (token.type) {
          case OPEN_PARENTHESIS:
            var subMatcher = parseList(makeAndMatcher, CLOSE_PARENTHESIS);
            matchers.push(subMatcher);
            break;
          case CLOSE_PARENTHESIS:
            if (waitForTokenType === CLOSE_PARENTHESIS) return makeMatcher(matchers);
            // misplaced )
            var previousMatcher = matchers[matchers.length - 1];
            if (!justSawWhitespace && previousMatcher != null && previousMatcher.fuzzyTerm != null) {
              // slap it on the back of the last guy
              previousMatcher.fuzzyTerm += token.text;
            } else {
              // it's its own term
              matchers.push(makeFuzzyTextMatcher(token.text));
            }
            break;
          case NOT:
            matchers.push(parseNot());
            break;
          case OR:
            var subMatcher = parseList(makeOrMatcher, CLOSE_PARENTHESIS);
            matchers.push(subMatcher);
            break;
          case LABEL:
            matchers.push(parseLabel());
            break;
          case QUOTED_THING:
            if (token.text.length !== 0) {
              matchers.push(makeExactTextMatcher(token.text));
            }
            break;
          case NORMAL_WORD:
            matchers.push(makeFuzzyTextMatcher(token.text));
            break;
        }
        var justSawWhitespace = token.type === WHITESPACE;
      }
      return makeMatcher(matchers);
    }

    function parseNot() {
      if (tokenIndex >= tokens.length) {
        // "not:" then EOF. treat it as a fuzzy matcher for "not:"
        return makeFuzzyTextMatcher(tokens[tokenIndex - 1].text);
      }
      var token = tokens[tokenIndex++];
      switch (token.type) {
        case WHITESPACE:
        case CLOSE_PARENTHESIS:
          // "not: " or "not:)"
          // Treat the "not:" as a fuzzy matcher,
          // and let the parent deal with this token
          tokenIndex--;
          return makeFuzzyTextMatcher(tokens[tokenIndex - 1].text);
        case OPEN_PARENTHESIS:
          // "not:("
          return makeNotMatcher(parseList(makeAndMatcher, CLOSE_PARENTHESIS));
        case NOT:
          // double negative all the way.
          return makeNotMatcher(parseNot());
        case OR:
          // "not:or("
          return makeNotMatcher(parseList(makeOrMatcher, CLOSE_PARENTHESIS));
        case LABEL:
          return makeNotMatcher(parseLabel());
        case QUOTED_THING:
          return makeNotMatcher(makeExactTextMatcher(token.text));
        case NORMAL_WORD:
          return makeNotMatcher(makeFuzzyTextMatcher(token.text));
      }
      throw new Error("unreachable");
    }

    function parseLabel() {
      if (tokenIndex >= tokens.length) {
        // "label:" then EOF. treat it as a fuzzy matcher for "label:"
        return makeFuzzyTextMatcher(tokens[tokenIndex - 1].text);
      }
      var token = tokens[tokenIndex++];
      switch (token.type) {
        case WHITESPACE:
        case CLOSE_PARENTHESIS:
          // "label: " or "label:)"
          // Treat the "label:" as a fuzzy matcher,
          // and let the parent deal with this token
          tokenIndex--;
          return makeFuzzyTextMatcher(tokens[tokenIndex - 1].text);
        case OPEN_PARENTHESIS: // "label:("
        case NOT:              // "label:not:"
        case OR:               // "label:or:("
        case LABEL:            // "label:label:"
        case QUOTED_THING:     // 'label:"Asdf"'
        case NORMAL_WORD:      // "label:Asdf"
          return makeLabelMatcher(token.text);
      }
      throw new Error("unreachable");
    }
  }

  function makeFuzzyTextMatcher(term) {
    // make this publicly modifiable
    fuzzyTextMatcher.fuzzyTerm = formatSearchable(term);;
    fuzzyTextMatcher.toString = function() {
      return "(fuzzy " + JSON.stringify(fuzzyTextMatcher.fuzzyTerm) + ")"
    };
    return fuzzyTextMatcher;
    function fuzzyTextMatcher(track) {
      return track.fuzzySearchTags.indexOf(fuzzyTextMatcher.fuzzyTerm) !== -1;
    }
  }
  function makeExactTextMatcher(term) {
    exactTextMatcher.toString = function() {
      return "(exact " + JSON.stringify(term) + ")"
    };
    return exactTextMatcher;
    function exactTextMatcher(track) {
      return track.exactSearchTags.indexOf(term) !== -1;
    }
  }
  function makeAndMatcher(children) {
    if (children.length === 1) return children[0];
    andMatcher.toString = function() {
      return "(" + children.join(" AND ") + ")";
    };
    return andMatcher;
    function andMatcher(track) {
      for (var i = 0; i < children.length; i++) {
        if (!children[i](track)) return false;
      }
      return true;
    }
  }
  function makeOrMatcher(children) {
    if (children.length === 1) return children[0];
    orMatcher.toString = function() {
      return "(" + children.join(" OR ") + ")";
    };
    return orMatcher;
    function orMatcher(track) {
      for (var i = 0; i < children.length; i++) {
        if (children[i](track)) return true;
      }
      return false;
    }
  }
  function makeNotMatcher(subMatcher) {
    notMatcher.toString = function() {
      return "(not " + subMatcher.toString() + ")";
    };
    return notMatcher;
    function notMatcher(track) {
      return !subMatcher(track);
    }
  }
  function makeLabelMatcher(text) {
    var id = (function() {
      for (var id in self.labelTable) {
        if (self.labelTable[id].name === text) {
          return id;
        }
      }
      return null;
    })();
    if (id != null) {
      labelMatcher.toString = function() {
        return "(label " + JSON.stringify(id) + ")";
      };
      return labelMatcher;
    } else {
      // not even a real label
      alwaysFail.toString = function() {
        return "(label <none>)";
      };
      return alwaysFail;
    }

    function labelMatcher(track) {
      return track.labels != null && track.labels[id];
    }
    function alwaysFail() {
      return false;
    }
  }

  function tokenizeQuery(query) {
    tokenizerRegex.lastIndex = 0;
    var tokens = [];
    while (true) {
      var match = tokenizerRegex.exec(query);
      if (match == null) break;
      var term = match[0];
      var type;
      for (var i = 1; i < match.length; i++) {
        if (match[i] != null) {
          type = i;
          break;
        }
      }
      switch (type) {
        case WHITESPACE:
        case OPEN_PARENTHESIS:
        case CLOSE_PARENTHESIS:
        case NOT:
        case OR:
        case LABEL:
          tokens.push({type: type, text: term});
          break;
        case QUOTED_THING:
        case NORMAL_WORD:
          var endParensCount = /\)*$/.exec(term)[0].length;
          term = term.substr(0, term.length - endParensCount);
          if (type === QUOTED_THING) {
            // strip quotes
            term = /^"(.*)"$/.exec(term)[1];
            // handle escapes
            term = term.replace(/\\(.)/g, "$1");
          }
          tokens.push({type: type, text: term});
          for (var i = 0; i < endParensCount; i++) {
            tokens.push({type: CLOSE_PARENTHESIS, text: ")"});
          }
          break;
      }
    }
    return tokens;
  }

};

function getOrCreate(key, table, initObjFunc) {
  var result = table[key];
  if (result == null) {
    result = initObjFunc();
    table[key] = result;
  }
  return result;
}

function moreThanOneKey(object){
  var count = -2;
  for (var k in object) {
    if (!++count) {
      return true;
    }
  }
  return false;
}

function formatSearchable(str) {
  return removeDiacritics(str).toLowerCase();
}

return MusicLibraryIndex;
