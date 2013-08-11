var path = require('path');
var osenv = require('osenv');

module.exports = MpdConf;

function MpdConf(state) {
  this.state = state;
  if (this.state == null) this.setDefaultState();
}

MpdConf.prototype.setDefaultState = function(){
  this.state = {};
  this.state.audio_httpd = {
    format: 'ogg',
    quality: 6,
    port: 16243
  };
  this.state.audio_pulse = null;
  this.state.audio_alsa = null;
  this.state.audio_oss = null;
  this.state.run_dir = null;
  this.state.music_directory = path.join(osenv.home(), 'music');
};

MpdConf.prototype.playlistDirectory = function(){
  return path.join(this.state.run_dir, "playlists");
};

MpdConf.prototype.setRunDir = function(it){
  this.state.run_dir = path.resolve(it);
};

MpdConf.prototype.toMpdConf = function(){
  var quality, bitrate, encoder_value;
  var audio_outputs = [];
  if (this.state.audio_httpd != null) {
    if (this.state.audio_httpd.format === 'ogg') {
      quality = "quality \"" + this.state.audio_httpd.quality + "\"";
      bitrate = "";
      encoder_value = "vorbis";
    } else if (this.state.audio_httpd.format === 'mp3') {
      quality = "";
      bitrate = "bitrate \"" + this.state.audio_httpd.bitrate + "\"";
      encoder_value = "lame";
    }
    audio_outputs.push("audio_output {\n" +
        "  type            \"httpd\"\n" +
        "  name            \"Groove Basin (httpd)\"\n" +
        "  encoder         \"" + encoder_value + "\"\n" +
        "  port            \"" + this.state.audio_httpd.port + "\"\n" +
        "  bind_to_address \"0.0.0.0\"\n" +
        "  " + quality + "\n" +
        "  " + bitrate + "\n" +
        "  format          \"44100:16:2\"\n" +
        "  max_clients     \"0\"\n" +
        "}");
  }
  if (this.state.audio_pulse != null) {
    audio_outputs.push("audio_output {\n" +
        "  type \"pulse\"\n" +
        "  name \"Groove Basin (pulse)\"\n" +
        "}");
  }
  if (this.state.audio_alsa != null) {
    audio_outputs.push("audio_output {\n" +
        "  type \"alsa\"\n" +
        "  name \"Groove Basin (alsa)\"\n" +
        "}");
  }
  if (this.state.audio_oss != null) {
    audio_outputs.push("audio_output {\n" +
        "  type \"oss\"\n" +
        "  name \"Groove Basin (oss)\"\n" +
        "}");
  }
  if (!audio_outputs.length) {
    audio_outputs.push("audio_output {\n" +
        "  type \"null\"\n" +
        "  name \"Groove Basin (null)\"\n" +
        "}");
  }
  return "music_directory         \"" + this.state.music_directory + "\"\n" +
      "playlist_directory      \"" + this.playlistDirectory() + "\"\n" +
      "db_file                 \"" + path.join(this.state.run_dir, "mpd.music.db") + "\"\n" +
      "log_file                \"" + path.join(this.state.run_dir, "mpd.log") + "\"\n" +
      "pid_file                \"" + path.join(this.state.run_dir, "mpd.pid") + "\"\n" +
      "state_file              \"" + path.join(this.state.run_dir, "mpd.state") + "\"\n" +
      "sticker_file            \"" + path.join(this.state.run_dir, "mpd.sticker.db") + "\"\n" +
      "bind_to_address         \"" + path.join(this.state.run_dir, "mpd.socket") + "\"\n" +
      "gapless_mp3_playback    \"yes\"\n" +
      "auto_update             \"yes\"\n" +
      "default_permissions     \"read,add,control,admin\"\n" +
      "replaygain              \"album\"\n" +
      "volume_normalization    \"yes\"\n" +
      "max_command_list_size   \"16384\"\n" +
      "max_connections         \"10\"\n" +
      "max_output_buffer_size  \"16384\"\nid3v1_encoding          \"UTF-8\"\n" +
      audio_outputs.join("\n") +
      "\n" +
      "# this socket is just for debugging with telnet\n" +
      "bind_to_address         \"localhost\"\n" +
      "port                    \"16244\"\n";
};
