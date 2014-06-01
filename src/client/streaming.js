exports.getUrl = getUrl;
exports.toggleStatus = toggleStatus;
exports.init = init;
exports.currentlyBuffering = currentlyBuffering;

var tryingToStream = false;
var actuallyStreaming = false;
var stillBuffering = false;
var player = null;
var audio = new Audio();
audio.addEventListener('playing', onPlaying, false);

var $ = window.$;
var $streamBtn = $('#stream-btn');

document.getElementById('stream-btn-label').addEventListener('mousedown', onLabelDown, false);

function onLabelDown(event) {
  event.stopPropagation();
}

function currentlyBuffering() {
  return tryingToStream && actuallyStreaming && stillBuffering;
}

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

function renderStreamButton(){
  var label = getButtonLabel();
  $streamBtn
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

function clearBuffer() {
  if (tryingToStream) {
    tryingToStream = !tryingToStream;
    updatePlayer();
    tryingToStream = !tryingToStream;
    updatePlayer();
  }
}

function updatePlayer() {
  var shouldStream = tryingToStream && player.isPlaying === true;
  if (actuallyStreaming !== shouldStream) {
    if (shouldStream) {
      audio.src = getUrl();
      audio.load();
      audio.play();
      stillBuffering = true;
    } else {
      audio.pause();
      audio.src = "";
      audio.load();
      stillBuffering = false;
    }
    actuallyStreaming = shouldStream;
  }
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

  player.on('currentTrack', updatePlayer);
  socket.on('seek', clearBuffer);
  setUpUi();
}
