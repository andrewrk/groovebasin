(function(){
  var ref$, EventEmitter, slice$ = [].slice;
  var DIACRITICS_REMOVAL_MAP = [
    {
      base: 'a',
      regex: /[A\u0041\u24B6\uFF21\u00C0\u00C1\u00C2\u1EA6\u1EA4\u1EAA\u1EA8\u00C3\u0100\u0102\u1EB0\u1EAE\u1EB4\u1EB2\u0226\u01E0\u00C4\u01DE\u1EA2\u00C5\u01FA\u01CD\u0200\u0202\u1EA0\u1EAC\u1EB6\u1E00\u0104\u023A\u2C6F\u0061\u24D0\uFF41\u1E9A\u00E0\u00E1\u00E2\u1EA7\u1EA5\u1EAB\u1EA9\u00E3\u0101\u0103\u1EB1\u1EAF\u1EB5\u1EB3\u0227\u01E1\u00E4\u01DF\u1EA3\u00E5\u01FB\u01CE\u0201\u0203\u1EA1\u1EAD\u1EB7\u1E01\u0105\u2C65\u0250]/g
    }, {
      base: 'aa',
      regex: /[\uA733\uA732]/g
    }, {
      base: 'ae',
      regex: /[\u00E6\u01FD\u01E3\u00C6\u01FC\u01E2]/g
    }, {
      base: 'ao',
      regex: /[\uA735\uA734]/g
    }, {
      base: 'au',
      regex: /[\uA737\uA736]/g
    }, {
      base: 'av',
      regex: /[\uA739\uA73B\uA738\uA73A]/g
    }, {
      base: 'ay',
      regex: /[\uA73D\uA73C]/g
    }, {
      base: 'b',
      regex: /[B\u0062\u24D1\uFF42\u1E03\u1E05\u1E07\u0180\u0183\u0253\u0042\u24B7\uFF22\u1E02\u1E04\u1E06\u0243\u0182\u0181]/g
    }, {
      base: 'c',
      regex: /[C\u0063\u24D2\uFF43\u0107\u0109\u010B\u010D\u00E7\u1E09\u0188\u023C\uA73F\u2184\u0043\u24B8\uFF23\u0106\u0108\u010A\u010C\u00C7\u1E08\u0187\u023B\uA73E]/g
    }, {
      base: 'd',
      regex: /[D\u0064\u24D3\uFF44\u1E0B\u010F\u1E0D\u1E11\u1E13\u1E0F\u0111\u018C\u0256\u0257\uA77A\u0044\u24B9\uFF24\u1E0A\u010E\u1E0C\u1E10\u1E12\u1E0E\u0110\u018B\u018A\u0189\uA779]/g
    }, {
      base: 'dz',
      regex: /[\u01F3\u01C6\u01F1\u01C4\u01F2\u01C5]/g
    }, {
      base: 'e',
      regex: /[E\u0065\u24D4\uFF45\u00E8\u00E9\u00EA\u1EC1\u1EBF\u1EC5\u1EC3\u1EBD\u0113\u1E15\u1E17\u0115\u0117\u00EB\u1EBB\u011B\u0205\u0207\u1EB9\u1EC7\u0229\u1E1D\u0119\u1E19\u1E1B\u0247\u025B\u01DD\u0045\u24BA\uFF25\u00C8\u00C9\u00CA\u1EC0\u1EBE\u1EC4\u1EC2\u1EBC\u0112\u1E14\u1E16\u0114\u0116\u00CB\u1EBA\u011A\u0204\u0206\u1EB8\u1EC6\u0228\u1E1C\u0118\u1E18\u1E1A\u0190\u018E]/g
    }, {
      base: 'f',
      regex: /[F\u0066\u24D5\uFF46\u1E1F\u0192\uA77C\u0046\u24BB\uFF26\u1E1E\u0191\uA77B]/g
    }, {
      base: 'g',
      regex: /[G\u0067\u24D6\uFF47\u01F5\u011D\u1E21\u011F\u0121\u01E7\u0123\u01E5\u0260\uA7A1\u1D79\uA77F\u0047\u24BC\uFF27\u01F4\u011C\u1E20\u011E\u0120\u01E6\u0122\u01E4\u0193\uA7A0\uA77D\uA77E]/g
    }, {
      base: 'h',
      regex: /[H\u0068\u24D7\uFF48\u0125\u1E23\u1E27\u021F\u1E25\u1E29\u1E2B\u1E96\u0127\u2C68\u2C76\u0265\u0048\u24BD\uFF28\u0124\u1E22\u1E26\u021E\u1E24\u1E28\u1E2A\u0126\u2C67\u2C75\uA78D]/g
    }, {
      base: 'hv',
      regex: /[\u0195]/g
    }, {
      base: 'i',
      regex: /[I\u0069\u24D8\uFF49\u00EC\u00ED\u00EE\u0129\u012B\u012D\u00EF\u1E2F\u1EC9\u01D0\u0209\u020B\u1ECB\u012F\u1E2D\u0268\u0131\u0049\u24BE\uFF29\u00CC\u00CD\u00CE\u0128\u012A\u012C\u0130\u00CF\u1E2E\u1EC8\u01CF\u0208\u020A\u1ECA\u012E\u1E2C\u0197]/g
    }, {
      base: 'j',
      regex: /[J\u006A\u24D9\uFF4A\u0135\u01F0\u0249\u004A\u24BF\uFF2A\u0134\u0248]/g
    }, {
      base: 'k',
      regex: /[K\u006B\u24DA\uFF4B\u1E31\u01E9\u1E33\u0137\u1E35\u0199\u2C6A\uA741\uA743\uA745\uA7A3\u004B\u24C0\uFF2B\u1E30\u01E8\u1E32\u0136\u1E34\u0198\u2C69\uA740\uA742\uA744\uA7A2]/g
    }, {
      base: 'l',
      regex: /[L\u006C\u24DB\uFF4C\u0140\u013A\u013E\u1E37\u1E39\u013C\u1E3D\u1E3B\u017F\u0142\u019A\u026B\u2C61\uA749\uA781\uA747\u004C\u24C1\uFF2C\u013F\u0139\u013D\u1E36\u1E38\u013B\u1E3C\u1E3A\u0141\u023D\u2C62\u2C60\uA748\uA746\uA780]/g
    }, {
      base: 'lj',
      regex: /[\u01C9\u01C7\u01C8]/g
    }, {
      base: 'm',
      regex: /[M\u006D\u24DC\uFF4D\u1E3F\u1E41\u1E43\u0271\u026F\u004D\u24C2\uFF2D\u1E3E\u1E40\u1E42\u2C6E\u019C]/g
    }, {
      base: 'n',
      regex: /[N\u006E\u24DD\uFF4E\u01F9\u0144\u00F1\u1E45\u0148\u1E47\u0146\u1E4B\u1E49\u019E\u0272\u0149\uA791\uA7A5\u004E\u24C3\uFF2E\u01F8\u0143\u00D1\u1E44\u0147\u1E46\u0145\u1E4A\u1E48\u0220\u019D\uA790\uA7A4]/g
    }, {
      base: 'nj',
      regex: /[\u01CC\u01CA\u01CB]/g
    }, {
      base: 'o',
      regex: /[O\u006F\u24DE\uFF4F\u00F2\u00F3\u00F4\u1ED3\u1ED1\u1ED7\u1ED5\u00F5\u1E4D\u022D\u1E4F\u014D\u1E51\u1E53\u014F\u022F\u0231\u00F6\u022B\u1ECF\u0151\u01D2\u020D\u020F\u01A1\u1EDD\u1EDB\u1EE1\u1EDF\u1EE3\u1ECD\u1ED9\u01EB\u01ED\u00F8\u01FF\u0254\uA74B\uA74D\u0275\u004F\u24C4\uFF2F\u00D2\u00D3\u00D4\u1ED2\u1ED0\u1ED6\u1ED4\u00D5\u1E4C\u022C\u1E4E\u014C\u1E50\u1E52\u014E\u022E\u0230\u00D6\u022A\u1ECE\u0150\u01D1\u020C\u020E\u01A0\u1EDC\u1EDA\u1EE0\u1EDE\u1EE2\u1ECC\u1ED8\u01EA\u01EC\u00D8\u01FE\u0186\u019F\uA74A\uA74C]/g
    }, {
      base: 'oi',
      regex: /[\u01A3\u01A2]/g
    }, {
      base: 'oo',
      regex: /[\uA74F\uA74E]/g
    }, {
      base: 'ou',
      regex: /[\u0223\u0222]/g
    }, {
      base: 'p',
      regex: /[P\u0070\u24DF\uFF50\u1E55\u1E57\u01A5\u1D7D\uA751\uA753\uA755\u0050\u24C5\uFF30\u1E54\u1E56\u01A4\u2C63\uA750\uA752\uA754]/g
    }, {
      base: 'q',
      regex: /[Q\u0071\u24E0\uFF51\u024B\uA757\uA759\u0051\u24C6\uFF31\uA756\uA758\u024A]/g
    }, {
      base: 'r',
      regex: /[R\u0072\u24E1\uFF52\u0155\u1E59\u0159\u0211\u0213\u1E5B\u1E5D\u0157\u1E5F\u024D\u027D\uA75B\uA7A7\uA783\u0052\u24C7\uFF32\u0154\u1E58\u0158\u0210\u0212\u1E5A\u1E5C\u0156\u1E5E\u024C\u2C64\uA75A\uA7A6\uA782]/g
    }, {
      base: 's',
      regex: /[S\u0073\u24E2\uFF53\u00DF\u015B\u1E65\u015D\u1E61\u0161\u1E67\u1E63\u1E69\u0219\u015F\u023F\uA7A9\uA785\u1E9B\u0053\u24C8\uFF33\u1E9E\u015A\u1E64\u015C\u1E60\u0160\u1E66\u1E62\u1E68\u0218\u015E\u2C7E\uA7A8\uA784]/g
    }, {
      base: 't',
      regex: /[T\u0074\u24E3\uFF54\u1E6B\u1E97\u0165\u1E6D\u021B\u0163\u1E71\u1E6F\u0167\u01AD\u0288\u2C66\uA787\u0054\u24C9\uFF34\u1E6A\u0164\u1E6C\u021A\u0162\u1E70\u1E6E\u0166\u01AC\u01AE\u023E\uA786]/g
    }, {
      base: 'tz',
      regex: /[\uA729\uA728]/g
    }, {
      base: 'u',
      regex: /[U\u0075\u24E4\uFF55\u00F9\u00FA\u00FB\u0169\u1E79\u016B\u1E7B\u016D\u00FC\u01DC\u01D8\u01D6\u01DA\u1EE7\u016F\u0171\u01D4\u0215\u0217\u01B0\u1EEB\u1EE9\u1EEF\u1EED\u1EF1\u1EE5\u1E73\u0173\u1E77\u1E75\u0289\u0055\u24CA\uFF35\u00D9\u00DA\u00DB\u0168\u1E78\u016A\u1E7A\u016C\u00DC\u01DB\u01D7\u01D5\u01D9\u1EE6\u016E\u0170\u01D3\u0214\u0216\u01AF\u1EEA\u1EE8\u1EEE\u1EEC\u1EF0\u1EE4\u1E72\u0172\u1E76\u1E74\u0244]/g
    }, {
      base: 'v',
      regex: /[V\u0076\u24E5\uFF56\u1E7D\u1E7F\u028B\uA75F\u028C\u0056\u24CB\uFF36\u1E7C\u1E7E\u01B2\uA75E\u0245]/g
    }, {
      base: 'vy',
      regex: /[\uA761\uA760]/g
    }, {
      base: 'w',
      regex: /[W\u0077\u24E6\uFF57\u1E81\u1E83\u0175\u1E87\u1E85\u1E98\u1E89\u2C73\u0057\u24CC\uFF37\u1E80\u1E82\u0174\u1E86\u1E84\u1E88\u2C72]/g
    }, {
      base: 'x',
      regex: /[X\u0078\u24E7\uFF58\u1E8B\u1E8D\u0058\u24CD\uFF38\u1E8A\u1E8C]/g
    }, {
      base: 'y',
      regex: /[Y\u0079\u24E8\uFF59\u1EF3\u00FD\u0177\u1EF9\u0233\u1E8F\u00FF\u1EF7\u1E99\u1EF5\u01B4\u024F\u1EFF\u0059\u24CE\uFF39\u1EF2\u00DD\u0176\u1EF8\u0232\u1E8E\u0178\u1EF6\u1EF4\u01B3\u024E\u1EFE]/g
    }, {
      base: 'z',
      regex: /[Z\u007A\u24E9\uFF5A\u017A\u1E91\u017C\u017E\u1E93\u1E95\u01B6\u0225\u0240\u2C6C\uA763\u005A\u24CF\uFF3A\u0179\u1E90\u017B\u017D\u1E92\u1E94\u01B5\u0224\u2C7F\u2C6B\uA762]/g
    }
  ];
  function removeDiacritics(str){
    var i$, ref$, len$, ref1$, regex, base;
    for (i$ = 0, len$ = (ref$ = DIACRITICS_REMOVAL_MAP).length; i$ < len$; ++i$) {
      ref1$ = ref$[i$], regex = ref1$.regex, base = ref1$.base;
      str = str.replace(regex, base);
    }
    return str;
  }
  var PREFIXES_TO_STRIP = [/^\s*the\s+/, /^\s*a\s+/, /^\s*an\s+/];
  function stripPrefixes(str){
    var i$, ref$, len$, regex;
    for (i$ = 0, len$ = (ref$ = PREFIXES_TO_STRIP).length; i$ < len$; ++i$) {
      regex = ref$[i$];
      str = str.replace(regex, '');
      break;
    }
    return str;
  }
  var MPD_SENTINEL = /^(OK|ACK|list_OK)(.*)$/m;
  function elapsedToDate(elapsed){
    return new Date(new Date - elapsed * 1000);
  }
  function dateToElapsed(date){
    return (new Date() - date) / 1000;
  }
  function fromMpdVol(vol){
    vol = parseInt(vol, 10);
    if (vol < 0 || vol > 100) {
      return null;
    } else {
      return vol / 100;
    }
  }
  function toMpdVol(vol){
    var ref$;
    return (ref$ = 0 > (ref$ = Math.round(parseFloat(vol, 10) * 100)) ? 0 : ref$) < 100 ? ref$ : 100;
  }
  function sortableTitle(title){
    return stripPrefixes(removeDiacritics(title));
  }
  function titleCompare(a, b){
    var _a, _b;
    _a = sortableTitle(a);
    _b = sortableTitle(b);
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
  }
  function noop(err){
    if (err) {
      throw err;
    }
  }
  function qEscape(str){
    return str.toString().replace(/"/g, '\\"');
  }
  function sign(n){
    if (n > 0) {
      return 1;
    } else if (n < 0) {
      return -1;
    } else {
      return 0;
    }
  }
  function parseMaybeUndefNumber(n){
    n = parseInt(n, 10);
    if (isNaN(n)) {
      n = null;
    }
    return n;
  }
  function splitOnce(line, separator){
    var index;
    index = line.indexOf(separator);
    return [line.substr(0, index), line.substr(index + separator.length)];
  }
  function parseWithSepField(msg, sep_field, skip_fields, flush){
    var current_obj, i$, ref$, len$, line, ref1$, key, value;
    if (msg === "") {
      return [];
    }
    current_obj = null;
    function flushCurrentObj(){
      if (current_obj != null) {
        flush(current_obj);
      }
      current_obj = {};
    }
    for (i$ = 0, len$ = (ref$ = msg.split("\n")).length; i$ < len$; ++i$) {
      line = ref$[i$];
      ref1$ = splitOnce(line, ': '), key = ref1$[0], value = ref1$[1];
      if (key in skip_fields) {
        continue;
      }
      if (key === sep_field) {
        flushCurrentObj();
      }
      current_obj[key] = value;
    }
    return flushCurrentObj();
  }
  function getOrCreate(key, table, initObjFunc){
    var result;
    result = table[key];
    if (result == null) {
      result = initObjFunc();
      table[key] = result;
    }
    return result;
  }
  function trackComparator(a, b){
    if (a.track < b.track) {
      return -1;
    } else if (a.track > b.track) {
      return 1;
    } else {
      return titleCompare(a.name, b.name);
    }
  }
  function albumComparator(a, b){
    if (a.year < b.year) {
      return -1;
    } else if (a.year > b.year) {
      return 1;
    } else {
      return titleCompare(a.name, b.name);
    }
  }
  function artistComparator(a, b){
    return titleCompare(a.name, b.name);
  }
  function playlistComparator(a, b){
    return titleCompare(a.name, b.name);
  }
  function albumKey(track){
    if (track.album_name) {
      return removeDiacritics(track.album_name);
    } else {
      return removeDiacritics(track.album_artist_name);
    }
  }
  function artistKey(artist_name){
    return removeDiacritics(artist_name);
  }
  function moreThanOneKey(object){
    var count, k;
    count = -2;
    for (k in object) {
      if (!++count) {
        return true;
      }
    }
    return false;
  }
  var next_id = 0;
  function nextId(){
    return "id-" + next_id++;
  }
  if ((EventEmitter = typeof require == 'function' ? (ref$ = require('events')) != null ? ref$.EventEmitter : void 8 : void 8) == null) {
    EventEmitter = (function(){
      EventEmitter.displayName = 'EventEmitter';
      var prototype = EventEmitter.prototype, constructor = EventEmitter;
      constructor.count = 0;
      function EventEmitter(){
        var this$ = this instanceof ctor$ ? this : new ctor$;
        this$.event_handlers = {};
        this$.next_id = 0;
        this$.prop = "__EventEmitter_" + constructor.count++ + "_id";
        return this$;
      } function ctor$(){} ctor$.prototype = prototype;
      prototype.on = function(event_name, handler){
        var ref$;
        handler[this.prop] = this.next_id;
        ((ref$ = this.event_handlers)[event_name] || (ref$[event_name] = {}))[this.next_id] = handler;
        this.next_id += 1;
      };
      prototype.removeListener = function(event_name, handler){
        var ref$;
        delete ((ref$ = this.event_handlers)[event_name] || (ref$[event_name] = {}))[handler[this.prop]];
      };
      prototype.emit = function(event_name){
        var args, id, ref$, ref1$, h;
        args = slice$.call(arguments, 1);
        for (id in ref$ = (ref1$ = this.event_handlers)[event_name] || (ref1$[event_name] = {})) {
          h = ref$[id];
          h.apply(null, args);
        }
      };
      return EventEmitter;
    }());
  }
  function addSearchTags(tracks){
    var i$, len$, track;
    for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
      track = tracks[i$];
      track.search_tags = removeDiacritics([track.artist_name, track.album_artist_name, track.album_name, track.name, track.file].join("\n"));
    }
  }
  var VARIOUS_ARTISTS_KEY = "VariousArtists";
  var VARIOUS_ARTISTS_NAME = "Various Artists";
  function operatorCompare(a, b){
    if (a === b) {
      return 0;
    }
    if (a < b) {
      return -1;
    } else {
      return 1;
    }
  }
  function makeCompareProps(props){
    return function(a, b){
      var i$, ref$, len$, prop, result;
      for (i$ = 0, len$ = (ref$ = props).length; i$ < len$; ++i$) {
        prop = ref$[i$];
        result = operatorCompare(a[prop], b[prop]);
        if (result) {
          return result;
        }
      }
      return 0;
    };
  }
  var compareSortKeyAndId = makeCompareProps(['sort_key', 'id']);
  var PlayerClient = (function(superclass){
    PlayerClient.displayName = 'PlayerClient';
    var prototype = extend$(PlayerClient, superclass).prototype, constructor = PlayerClient;
    function PlayerClient(socket){
      var this$ = this instanceof ctor$ ? this : new ctor$;
      this$.socket = socket;
      superclass.call(this$);
      this$.resetServerState();
      this$.updateFuncs = {
        stored_playlist: bind$(this$, 'updateStoredPlaylists'),
        playlist: bind$(this$, 'updatePlaylist'),
        player: bind$(this$, 'updateStatus'),
        mixer: bind$(this$, 'updateStatus'),
        sticker: function(){
          return this$.emit('stickerupdate');
        }
      };
      this$.socket.on('PlayerResponse', function(data){
        this$.handleResponse(JSON.parse(data));
      });
      this$.socket.on('PlayerStatus', function(data){
        this$.handleStatus(JSON.parse(data));
      });
      this$.socket.on('disconnect', function(){
        this$.resetServerState();
      });
      if (this$.socket.socket.connected) {
        this$.handleConnectionStart();
      } else {
        this$.socket.on('connect', bind$(this$, 'handleConnectionStart'));
      }
      return this$;
    } function ctor$(){} ctor$.prototype = prototype;
    prototype.handleConnectionStart = function(){
      var this$ = this;
      this.updateLibrary(function(){
        this$.updateStatus();
        this$.updatePlaylist();
        this$.updateStoredPlaylists();
      });
    };
    prototype.updateLibrary = function(callback){
      var this$ = this;
      callback == null && (callback = noop);
      this.sendCommandName('listallinfo', function(err, track_table){
        var res$, _, track, tracks, last_query;
        if (err) {
          return callback(err);
        }
        res$ = [];
        for (_ in track_table) {
          track = track_table[_];
          res$.push(track);
        }
        tracks = res$;
        addSearchTags(tracks);
        this$.buildArtistAlbumTree(tracks, this$.library);
        this$.have_file_list_cache = true;
        last_query = this$.last_query;
        this$.last_query = "";
        this$.search(last_query);
        callback();
      });
    };
    prototype.updatePlaylist = function(callback){
      var this$ = this;
      callback = callback || noop;
      this.sendCommandName('playlistinfo', function(err, tracks){
        if (err) return callback(err);
        this$.clearPlaylist();
        for (var id in tracks) {
          var item = tracks[id];
          this$.playlist.item_table[id] = {
            id: id,
            sort_key: item.sort_key,
            is_random: item.is_random,
            track: this$.library.track_table[item.file],
            playlist: this$.playlist
          };
        }
        this$.refreshPlaylistList();
        if (this$.status.current_item != null) {
          this$.status.current_item = this$.playlist.item_table[this$.status.current_item.id];
        }
        if (this$.status.current_item != null) {
          this$.emit('playlistupdate');
          callback();
        } else {
          this$.updateStatus(function(err){
            if (err) {
              return callback(err);
            }
            this$.emit('playlistupdate');
            callback();
          });
        }
      });
    };
    prototype.updateStoredPlaylists = function(callback){
      var this$ = this;
      callback == null && (callback = noop);
      this.sendCommand("listplaylists", function(err, msg){
        var count, stored_playlist_table, stored_playlist_item_table;
        if (err) {
          return callback(err);
        }
        count = 0;
        stored_playlist_table = {};
        stored_playlist_item_table = {};
        parseWithSepField(msg, 'playlist', {}, function(obj){
          var name;
          name = obj.playlist;
          count += 1;
          updateStoredPlaylist(name, function(err){
            if (count == null) {
              return;
            }
            if (err) {
              count = null;
              return cb(err);
            }
            count -= 1;
            finishUp();
          });
        });
        finishUp();
        function finishUp(){
          var res$, k, ref$, v, i, len$, playlist;
          if (count === 0) {
            this$.stored_playlist_table = stored_playlist_table;
            this$.stored_playlist_item_table = stored_playlist_item_table;
            res$ = [];
            for (k in ref$ = stored_playlist_table) {
              v = ref$[k];
              res$.push(v);
            }
            this$.stored_playlists = res$;
            this$.stored_playlists.sort(playlistComparator);
            for (i = 0, len$ = (ref$ = this$.stored_playlists).length; i < len$; ++i) {
              playlist = ref$[i];
              playlist.pos = i;
            }
            callback();
            return this$.emit('storedplaylistupdate');
          }
        }
        function updateStoredPlaylist(name, callback){
          this$.sendCommand("listplaylist \"" + qEscape(name) + "\"", function(err, msg){
            var item_list, item_table, playlist;
            if (err) {
              return callback(err);
            }
            item_list = [];
            item_table = {};
            playlist = {
              name: name,
              item_list: item_list,
              item_table: item_table
            };
            parseWithSepField(msg, 'file', {}, function(item){
              item = {
                track: this$.library.track_table[item.file],
                pos: item_list.length,
                id: nextId(),
                playlist: playlist
              };
              item_list.push(item);
              item_table[item.id] = item;
              stored_playlist_item_table[item.id] = item;
            });
            stored_playlist_table[name] = playlist;
            callback();
          });
        }
      });
    };
    prototype.updateStatus = function(callback){
      var this$ = this;
      callback = callback || noop;
      this.sendCommandName('status', function(err, o){
        var ref$;
        if (err) {
          return callback(err);
        }
        ref$ = this$.status;
        ref$.volume = o.volume;
        ref$.repeat = o.repeat;
        ref$.single = o.single;
        ref$.state = o.state;
        this$.status.track_start_date = o.track_start_date != null ? new Date(o.track_start_date) : null;
        this$.status.paused_time = o.paused_time;
      });
      this.sendCommandName('currentsong', function(err, id){
        if (err) return callback(err);
        if (id != null) {
          this$.status.current_item = this$.playlist.item_table[id];
          if (this$.status.current_item != null) {
            this$.status.time = this$.status.current_item.track.time;
            this$.emit('statusupdate');
            callback();
          } else {
            this$.status.current_item = null;
            this$.updatePlaylist(function(err){
              if (err) {
                return callback(err);
              }
              this$.emit('statusupdate');
              callback();
            });
          }
        } else {
          this$.status.current_item = null;
          callback();
          this$.emit('statusupdate');
        }
      });
    };
    prototype.search = function(query){
      var words, result, k, ref$, track, is_match;
      query = query.trim();
      if (query.length === 0) {
        this.search_results = this.library;
        this.emit('libraryupdate');
        this.last_query = query;
        return;
      }
      words = removeDiacritics(query).split(/\s+/);
      query = words.join(" ");
      if (query === this.last_query) {
        return;
      }
      this.last_query = query;
      result = [];
      for (k in ref$ = this.library.track_table) {
        track = ref$[k];
        is_match = fn$();
        if (is_match) {
          result.push(track);
        }
      }
      this.buildArtistAlbumTree(result, this.search_results = {});
      this.emit('libraryupdate');
      function fn$(){
        var i$, ref$, len$, word;
        for (i$ = 0, len$ = (ref$ = words).length; i$ < len$; ++i$) {
          word = ref$[i$];
          if (track.search_tags.indexOf(word) === -1) {
            return false;
          }
        }
        return true;
      }
    };
    prototype.getDefaultQueuePosition = function(){
      var ref$, previous_key, next_key, start_pos, i, to$, track, sort_key;
      previous_key = (ref$ = this.status.current_item) != null ? ref$.sort_key : void 8;
      next_key = null;
      start_pos = ((ref$ = (ref$ = this.status.current_item) != null ? ref$.pos : void 8) != null
        ? ref$
        : -1) + 1;
      for (i = start_pos, to$ = this.playlist.item_list.length; i < to$; ++i) {
        track = this.playlist.item_list[i];
        sort_key = track.sort_key;
        if (track.is_random) {
          next_key = sort_key;
          break;
        }
        previous_key = sort_key;
      }
      return {
        previous_key: previous_key,
        next_key: next_key
      };
    };
    prototype.generateSortKey = function(previous_key, next_key){
      if (previous_key != null) {
        if (next_key != null) {
          return (previous_key + next_key) / 2;
        } else {
          return 0 | previous_key + 1;
        }
      } else {
        if (next_key != null) {
          return (0 + next_key) / 2;
        } else {
          return 1;
        }
      }
    };
    prototype.generateId = function(){
      var result, _, shortHex, __;
      result = "";
      for (_ = 0; _ < 8; ++_) {
        shortHex = (0 | Math.random() * 0x10000).toString(16);
        for (__ = shortHex.length; __ < 4; ++__) {
          result += "0";
        }
        result += shortHex;
      }
      return result;
    };
    prototype.queueFiles = function(files, previous_key, next_key, is_random){
      var ref$, items, i$, len$, file, sort_key, id;
      if (!files.length) {
        return;
      }
      is_random = Boolean(is_random);
      if (previous_key == null && next_key == null) {
        ref$ = this.getDefaultQueuePosition(), previous_key = ref$.previous_key, next_key = ref$.next_key;
      }
      items = {};
      for (i$ = 0, len$ = files.length; i$ < len$; ++i$) {
        file = files[i$];
        sort_key = this.generateSortKey(previous_key, next_key);
        id = this.generateId();
        items[id] = {
          file: file,
          sort_key: sort_key,
          is_random: is_random
        };
        this.playlist.item_table[id] = {
          id: id,
          sort_key: sort_key,
          is_random: is_random,
          track: this.library.track_table[file]
        };
        previous_key = sort_key;
      }
      this.refreshPlaylistList();
      this.sendCommand({
        name: 'addid',
        items: items
      });
      this.emit('playlistupdate');
    };
    prototype.queueFilesNext = function(files){
      var ref$, previous_key, next_key, i$, len$, track;
      previous_key = (ref$ = this.status.current_item) != null ? ref$.sort_key : void 8;
      next_key = null;
      for (i$ = 0, len$ = (ref$ = this.playlist.item_list).length; i$ < len$; ++i$) {
        track = ref$[i$];
        if (previous_key == null || track.sort_key > previous_key) {
          if (next_key == null || track.sort_key < next_key) {
            next_key = track.sort_key;
          }
        }
      }
      this.queueFiles(files, previous_key, next_key);
    };
    prototype.clear = function(){
      this.sendCommandName('clear');
      this.clearPlaylist();
      this.emit('playlistupdate');
    };
    prototype.shuffle = function(){
      this.sendCommandName('shuffle');
    };
    prototype.stop = function(){
      this.sendCommandName('stop');
      this.status.state = "stop";
      this.emit('statusupdate');
    };
    prototype.play = function(){
      this.sendCommandName('play');
      if (this.status.state === "pause") {
        this.status.track_start_date = elapsedToDate(this.status.paused_time);
        this.status.state = "play";
        this.emit('statusupdate');
      }
    };
    prototype.pause = function(){
      this.sendCommandName('pause');
      if (this.status.state === "play") {
        this.status.paused_time = dateToElapsed(this.status.track_start_date);
        this.status.state = "pause";
        this.emit('statusupdate');
      }
    };
    prototype.next = function(){
      var ref$, current_pos, pos, id;
      current_pos = (ref$ = this.status.current_item) != null ? ref$.pos : void 8;
      if (current_pos != null) {
        pos = current_pos + 1;
      } else {
        pos = 0;
      }
      id = (ref$ = this.playlist.item_list[pos]) != null ? ref$.id : void 8;
      this.playId(id);
    };
    prototype.prev = function(){
      var ref$, current_pos, pos, id;
      current_pos = (ref$ = this.status.current_item) != null ? ref$.pos : void 8;
      if (current_pos != null) {
        pos = current_pos - 1;
      } else {
        pos = this.playlist.item_list.length - 1;
      }
      id = (ref$ = this.playlist.item_list[pos]) != null ? ref$.id : void 8;
      this.playId(id);
    };
    prototype.playId = function(track_id){
      this.sendCommand({
        name: 'playid',
        track_id: track_id
      });
      this.anticipatePlayId(track_id);
    };
    prototype.moveIds = function(track_ids, previous_key, next_key){
      var res$, i$, len$, id, track, tracks, items, sort_key;
      res$ = [];
      for (i$ = 0, len$ = track_ids.length; i$ < len$; ++i$) {
        id = track_ids[i$];
        if ((track = this.playlist.item_table[id]) != null) {
          res$.push(track);
        }
      }
      tracks = res$;
      tracks.sort(compareSortKeyAndId);
      items = {};
      for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
        track = tracks[i$];
        sort_key = this.generateSortKey(previous_key, next_key);
        items[id] = {
          sort_key: sort_key
        };
        track.sort_key = sort_key;
        previous_key = sort_key;
      }
      this.refreshPlaylistList();
      this.sendCommand({
        name: 'move',
        items: items
      });
      this.emit('playlistupdate');
    };
    prototype.shiftIds = function(track_id_set, offset){
      var items, previous_key, next_key, i$, ref$, len$, track, sort_key;
      items = {};
      previous_key = null;
      next_key = null;
      if (offset < 0) {
        for (i$ = 0, len$ = (ref$ = this.playlist.item_list).length; i$ < len$; ++i$) {
          track = ref$[i$];
          if (track.id in track_id_set) {
            if (next_key == null) {
              continue;
            }
            sort_key = this.generateSortKey(previous_key, next_key);
            items[track.id] = {
              sort_key: sort_key
            };
            track.sort_key = sort_key;
          }
          previous_key = next_key;
          next_key = track.sort_key;
        }
      } else {
        for (i$ = (ref$ = this.playlist.item_list).length - 1; i$ >= 0; --i$) {
          track = ref$[i$];
          if (track.id in track_id_set) {
            if (previous_key == null) {
              continue;
            }
            sort_key = this.generateSortKey(previous_key, next_key);
            items[track.id] = {
              sort_key: sort_key
            };
            track.sort_key = sort_key;
          }
          next_key = previous_key;
          previous_key = track.sort_key;
        }
      }
      this.refreshPlaylistList();
      this.sendCommand({
        name: 'move',
        items: items
      });
      this.emit('playlistupdate');
    };
    prototype.removeIds = function(track_ids){
      var ids, i$, len$, track_id, ref$, item;
      if (track_ids.length === 0) {
        return;
      }
      ids = [];
      for (i$ = 0, len$ = track_ids.length; i$ < len$; ++i$) {
        track_id = track_ids[i$];
        if (((ref$ = this.status.current_item) != null ? ref$.id : void 8) === track_id) {
          this.status.current_item = null;
        }
        ids.push(track_id);
        item = this.playlist.item_table[track_id];
        delete this.playlist.item_table[item.id];
        this.refreshPlaylistList();
      }
      this.sendCommand({
        name: 'deleteid',
        ids: ids
      });
      this.emit('playlistupdate');
    };
    prototype.seek = function(pos){
      pos = parseFloat(pos, 10);
      if (pos < 0) pos = 0;
      if (pos > this.status.time) pos = this.status.time;
      this.sendCommand({
        name: 'seek',
        pos: pos
      });
      this.status.track_start_date = elapsedToDate(pos);
      this.emit('statusupdate');
    };
    prototype.setVolume = function(vol){
      vol = toMpdVol(vol);
      this.sendCommand("setvol " + vol);
      this.status.volume = fromMpdVol(vol);
      this.emit('statusupdate');
    };
    prototype.changeStatus = function(arg$){
      var repeat, single;
      repeat = arg$.repeat, single = arg$.single;
      this.status.repeat = repeat;
      this.status.single = single;
      this.sendCommand({
        name: 'repeat',
        repeat: repeat,
        single: single
      });
      this.emit('statusupdate');
    };
    prototype.getFileInfo = function(file, callback){
      var this$ = this;
      callback == null && (callback = noop);
      this.sendCommand("lsinfo \"" + qEscape(file) + "\"", function(err, track){
        if (err) {
          return callback(err);
        }
        callback(null, track);
      });
    };
    prototype.authenticate = function(password, callback){
      var this$ = this;
      callback == null && (callback = noop);
      this.sendCommand({
        name: 'password',
        password: password
      }, function(err){
        callback(err);
      });
    };
    prototype.scanFiles = function(files){
      var file;
      this.sendCommands((function(){
        var i$, ref$, len$, results$ = [];
        for (i$ = 0, len$ = (ref$ = files).length; i$ < len$; ++i$) {
          file = ref$[i$];
          results$.push("update \"" + qEscape(file) + "\"");
        }
        return results$;
      }()));
    };
    prototype.findStickers = function(dir, name, cb){
      var this$ = this;
      cb == null && (cb = noop);
      this.sendCommand("sticker find song \"" + qEscape(dir) + "\" \"" + qEscape(name) + "\"", function(err, msg){
        var current_file, stickers, i$, ref$, len$, line, ref1$, name, value;
        if (err) {
          return cb(err);
        }
        current_file = null;
        stickers = {};
        for (i$ = 0, len$ = (ref$ = msg.split("\n")).length; i$ < len$; ++i$) {
          line = ref$[i$];
          ref1$ = splitOnce(line, ": "), name = ref1$[0], value = ref1$[1];
          if (name === "file") {
            current_file = value;
          } else if (name === "sticker") {
            if (current_file == null) {
              return cb("protocol");
            }
            value = splitOnce(value, "=")[1];
            stickers[current_file] = value;
          }
        }
        cb(null, stickers);
      });
    };
    prototype.setStickers = function(files, name, value, cb){
      var res$, i$, len$, file, cmds, this$ = this;
      cb == null && (cb = noop);
      res$ = [];
      for (i$ = 0, len$ = files.length; i$ < len$; ++i$) {
        file = files[i$];
        res$.push("sticker set song \"" + qEscape(file) + "\" \"" + qEscape(name) + "\" \"" + qEscape(value) + "\"");
      }
      cmds = res$;
      this.sendCommands(cmds, function(err){
        return cb(err);
      });
    };
    prototype.queueFilesInStoredPlaylist = function(files, stored_playlist_name){
      var file;
      this.sendCommands((function(){
        var i$, ref$, len$, results$ = [];
        for (i$ = 0, len$ = (ref$ = files).length; i$ < len$; ++i$) {
          file = ref$[i$];
          results$.push("playlistadd \"" + qEscape(stored_playlist_name) + "\" \"" + qEscape(file) + "\"");
        }
        return results$;
      }()));
    };
    prototype.sendCommands = function(command_list, callback){
      callback == null && (callback = noop);
      if (command_list.length === 0) {
        return;
      }
      this.sendCommand("command_list_begin\n" + command_list.join("\n") + "\ncommand_list_end", callback);
    };
    prototype.sendCommandName = function(name, cb){
      cb == null && (cb = noop);
      this.sendCommand({
        name: name
      }, cb);
    };
    prototype.sendCommand = function(cmd, cb){
      var callback_id;
      cb == null && (cb = noop);
      callback_id = this.next_response_handler_id++;
      this.response_handlers[callback_id] = cb;
      this.socket.emit('request', JSON.stringify({
        cmd: cmd,
        callback_id: callback_id
      }));
    };
    prototype.handleResponse = function(arg){
      var err, msg, callback_id, handler;
      err = arg.err, msg = arg.msg, callback_id = arg.callback_id;
      handler = this.response_handlers[callback_id];
      delete this.response_handlers[callback_id];
      handler(err, msg);
    };
    prototype.handleStatus = function(systems){
      var i$, len$, system, ref$, updateFunc;
      for (i$ = 0, len$ = systems.length; i$ < len$; ++i$) {
        system = systems[i$];
        updateFunc = (ref$ = this.updateFuncs[system]) != null ? ref$ : noop;
        updateFunc();
      }
    };
    prototype.clearPlaylist = function(){
      this.playlist = {
        item_list: [],
        item_table: {},
        pos: null,
        name: null
      };
    };
    prototype.anticipatePlayId = function(track_id){
      var item;
      item = this.playlist.item_table[track_id];
      this.status.current_item = item;
      this.status.state = "play";
      this.status.time = item.track.time;
      this.status.track_start_date = new Date();
      this.emit('statusupdate');
    };
    prototype.anticipateSkip = function(direction){
      var ref$, next_item;
      next_item = this.playlist.item_list[((ref$ = this.status.current_item) != null ? ref$.pos : void 8) + direction];
      if (next_item != null) {
        this.anticipatePlayId(next_item.id);
      }
    };
    prototype.buildArtistAlbumTree = function(tracks, library){
      var i$, len$, track, album_key, album, artist_table, k, ref$, album_artists, i, ref1$, album_artist_name, artist_key, artist, various_artist;
      library.track_table = {};
      library.album_table = {};
      for (i$ = 0, len$ = tracks.length; i$ < len$; ++i$) {
        track = tracks[i$];
        library.track_table[track.file] = track;
        album_key = albumKey(track);
        album = getOrCreate(album_key, library.album_table, fn$);
        track.album = album;
        album.tracks.push(track);
        if (album.year == null) {
          album.year = track.year;
        }
      }
      artist_table = {};
      for (k in ref$ = library.album_table) {
        album = ref$[k];
        album_artists = {};
        album.tracks.sort(trackComparator);
        for (i = 0, len$ = (ref1$ = album.tracks).length; i < len$; ++i) {
          track = ref1$[i];
          track.pos = i;
          album_artist_name = track.album_artist_name;
          album_artists[artistKey(album_artist_name)] = true;
          album_artists[artistKey(track.artist_name)] = true;
        }
        if (moreThanOneKey(album_artists)) {
          album_artist_name = VARIOUS_ARTISTS_NAME;
          artist_key = VARIOUS_ARTISTS_KEY;
          for (i$ = 0, len$ = (ref1$ = album.tracks).length; i$ < len$; ++i$) {
            track = ref1$[i$];
            track.artist_disambiguation = track.artist_name;
          }
        } else {
          artist_key = artistKey(album_artist_name);
        }
        artist = getOrCreate(artist_key, artist_table, fn1$);
        album.artist = artist;
        artist.albums.push(album);
      }
      library.artists = [];
      various_artist = null;
      for (k in artist_table) {
        artist = artist_table[k];
        artist.albums.sort(albumComparator);
        for (i = 0, len$ = (ref$ = artist.albums).length; i < len$; ++i) {
          album = ref$[i];
          album.pos = i;
        }
        if (artist.key === VARIOUS_ARTISTS_KEY) {
          various_artist = artist;
        } else {
          library.artists.push(artist);
        }
      }
      library.artists.sort(artistComparator);
      if (various_artist != null) {
        library.artists.splice(0, 0, various_artist);
      }
      for (i = 0, len$ = (ref$ = library.artists).length; i < len$; ++i) {
        artist = ref$[i];
        artist.pos = i;
      }
      library.artist_table = artist_table;
      function fn$(){
        return {
          name: track.album_name,
          year: track.year,
          tracks: [],
          key: album_key
        };
      }
      function fn1$(){
        return {
          name: album_artist_name,
          albums: [],
          key: artist_key
        };
      }
    };
    prototype.refreshPlaylistList = function(){
      var id, ref$, item, i, len$;
      this.playlist.item_list = [];
      for (id in ref$ = this.playlist.item_table) {
        item = ref$[id];
        item.playlist = this.playlist;
        this.playlist.item_list.push(item);
      }
      this.playlist.item_list.sort(compareSortKeyAndId);
      for (i = 0, len$ = (ref$ = this.playlist.item_list).length; i < len$; ++i) {
        item = ref$[i];
        item.pos = i;
      }
    };
    prototype.resetServerState = function(){
      this.buffer = "";
      this.response_handlers = {};
      this.next_response_handler_id = 0;
      this.have_file_list_cache = false;
      this.library = {
        artists: [],
        track_table: {}
      };
      this.search_results = this.library;
      this.last_query = "";
      this.clearPlaylist();
      this.status = {
        current_item: null
      };
      this.stored_playlist_table = {};
      this.stored_playlist_item_table = {};
      this.stored_playlists = [];
    };
    return PlayerClient;
  }(EventEmitter));
  if (typeof module != 'undefined' && module !== null) {
    module.exports = PlayerClient;
  }
  if (typeof window != 'undefined' && window !== null) {
    window.PlayerClient = PlayerClient;
  }
  function extend$(sub, sup){
    function fun(){} fun.prototype = (sub.superclass = sup).prototype;
    (sub.prototype = new fun).constructor = sub;
    if (typeof sup.extended == 'function') sup.extended(sub);
    return sub;
  }
  function bind$(obj, key){
    return function(){ return obj[key].apply(obj, arguments) };
  }
}).call(this);
