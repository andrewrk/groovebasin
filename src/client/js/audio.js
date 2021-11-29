
const dom = require("dom");

function newAudio() {
    const element = new Audio();
    return dom.getElementHandle(element);
}

function setAudioSrc(handle, src) {
    dom.getElementByHandle(handle).src = src;
}
function loadAudio(handle) {
    dom.getElementByHandle(handle).load();
}
function playAudio(handle) {
    dom.getElementByHandle(handle).play();
}
function pauseAudio(handle) {
    dom.getElementByHandle(handle).pause();
}
function setAudioVolume(handle, volume) {
    dom.getElementByHandle(handle).volume = volume;
}

return {
    newAudio,
    setAudioSrc,
    loadAudio,
    playAudio,
    pauseAudio,
    setAudioVolume,
};
