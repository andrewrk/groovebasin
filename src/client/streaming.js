exports.getUrl = getUrl;
exports.toggleStatus = toggleStatus;
exports.init = init;

var trying_to_stream = false;
var actually_streaming = false;
var streaming_buffering = false;
var player = null;
var audio = new Audio();
audio.addEventListener('playing', onPlaying, false);

var $ = window.$;
var $stream_btn = $('#stream-btn');


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

function onPlaying() {
  streaming_buffering = false;
  renderStreamButton();
}

function updatePlayer() {
  var should_stream = trying_to_stream && player.state === "play";
  if (actually_streaming === should_stream) return;
  if (should_stream) {
    audio.src = getUrl();
    audio.load();
    audio.play();
    streaming_buffering = true;
  } else {
    audio.pause();
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

  player.on('statusupdate', updatePlayer);
  setUpUi();
}
