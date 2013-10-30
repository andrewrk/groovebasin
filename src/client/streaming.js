exports.getUrl = getUrl;
exports.toggleStatus = toggleStatus;
exports.init = init;

var trying_to_stream = false;
var actually_streaming = false;
var streaming_buffering = false;
var player = null;

var $ = window.$;
var $stream_btn = $('#stream-btn');

var soundManager = window.soundManager;


function getButtonLabel() {
  if (trying_to_stream) {
    if (actually_streaming) {
      if (streaming_buffering) {
        return "Stream: Buffering";
      } else {
        return "Stream: On";
      }
    } else {
      return "Stream: Paused";
    }
  } else {
    return "Stream: Off";
  }
}

function getButtonDisabled() {
  return false;
}

function renderStreamButton(){
  var label = getButtonLabel();
  $stream_btn
    .button("option", "disabled", getButtonDisabled())
    .button("option", "label", label)
    .prop("checked", trying_to_stream)
    .button("refresh");
}

function toggleStatus() {
  trying_to_stream = !trying_to_stream;
  renderStreamButton();
  updatePlayer();
  return false;
}

function getUrl(){
  return "/stream.mp3";
}

function updatePlayer() {
  var should_stream = trying_to_stream && player.state === "play";
  if (actually_streaming === should_stream) return;
  if (should_stream) {
    soundManager.destroySound('stream');
    var sound = soundManager.createSound({
      id: 'stream',
      url: getUrl(),
      onbufferchange: function(){
        streaming_buffering = sound.isBuffering;
        renderStreamButton();
      }
    });
    sound.play();
    streaming_buffering = sound.isBuffering;
  } else {
    soundManager.destroySound('stream');
    streaming_buffering = false;
  }
  actually_streaming = should_stream;
  renderStreamButton();
}

function setUpUi() {
  $stream_btn.button({
    icons: {
      primary: "ui-icon-signal-diag"
    }
  });
  $stream_btn.on('click', toggleStatus);
}

function init(playerInstance, socket) {
  player = playerInstance;

  soundManager.setup({
    url: "/vendor/soundmanager2/",
    flashVersion: 9,
    debugMode: false
  });
  player.on('statusupdate', updatePlayer);
  setUpUi();
}
