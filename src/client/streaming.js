exports.getUrl = getUrl;
exports.toggleStatus = toggleStatus;
exports.init = init;

var tryingToStream = false;
var actuallyStreaming = false;
var actuallyPlaying = false;
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

function getStreamerCount() {
  return player.streamers.anonCount + player.streamers.clientIds.length;
}

function getStatusLabel() {
  if (tryingToStream) {
    if (actuallyStreaming) {
      if (stillBuffering) {
        return "Buffering";
      } else {
        return "On";
      }
    } else {
      return "Paused";
    }
  } else {
    return "Off";
  }
}

function getButtonLabel() {
  return getStreamerCount() + " Stream: " + getStatusLabel();
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

function getUrl() {
  // keep the URL relative so that reverse proxies can work
  return "stream.mp3";
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
  if (actuallyStreaming !== tryingToStream || actuallyPlaying !== player.isPlaying) {
    if (tryingToStream) {
      audio.src = getUrl();
      audio.load();
      if (player.isPlaying) {
        audio.play();
        stillBuffering = true;
        actuallyPlaying = true;
      } else {
        audio.pause();
        stillBuffering = false;
        actuallyPlaying = false;
      }
    } else {
      audio.pause();
      audio.src = "";
      audio.load();
      stillBuffering = false;
      actuallyPlaying = false;
    }
    actuallyStreaming = tryingToStream;
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
  player.on('streamers', renderStreamButton);
  socket.on('seek', clearBuffer);
  setUpUi();
}
