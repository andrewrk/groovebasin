exports.getUrl = getUrl;
exports.toggleStatus = toggleStatus;
exports.init = init;

var tryingToStream = false;
var actuallyStreaming = false;
var stillBuffering = false;
var player = null;
var audio = new Audio();
audio.addEventListener('playing', onPlaying, false);

var $ = window.$;
var $streamBtn = $('#stream-btn');


function getButtonLabel() {
  if (tryingToStream) {
    if (actuallyStreaming) {
      if (stillBuffering) {
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
  $streamBtn
    .button("option", "disabled", getButtonDisabled())
    .button("option", "label", label)
    .prop("checked", tryingToStream)
    .button("refresh");
}

function toggleStatus() {
  tryingToStream = !tryingToStream;
  renderStreamButton();
  updatePlayer();
  return false;
}

function getUrl(){
  return "/stream.mp3";
}

function onPlaying() {
  stillBuffering = false;
  renderStreamButton();
}

function updatePlayer() {
  var shouldStream = tryingToStream && player.isPlaying === true;
  if (actuallyStreaming === shouldStream) return;
  if (shouldStream) {
    audio.src = getUrl();
    audio.load();
    audio.play();
    stillBuffering = true;
  } else {
    audio.pause();
    stillBuffering = false;
  }
  actuallyStreaming = shouldStream;
  renderStreamButton();
}

function setUpUi() {
  $streamBtn.button({
    icons: {
      primary: "ui-icon-signal-diag"
    }
  });
  $streamBtn.on('click', toggleStatus);
}

function init(playerInstance, socket) {
  player = playerInstance;

  player.on('statusupdate', updatePlayer);
  setUpUi();
}
