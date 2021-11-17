
const {HandleRegistry} = require("handleRegistry");

const elementRegistry = new HandleRegistry();

function getElementById(id) {
    const element = document.getElementById(id);
    if (element == null) throw new Error("no such element: " + id);
    const {handle} = elementRegistry.alloc(element);
    return handle;
}

function releaseElementHandle(handle) {
    elementRegistry.disposeHandle(handle);
}

function setElementShown(handle, shown) {
    elementRegistry.registry[handle].style.display = shown ? "" : "none";
}

function setElementTextContent(handle, text) {
    elementRegistry.registry[handle].textContent = text;
}

return {
    getElementById,
    releaseElementHandle,
    setElementShown,
    setElementTextContent,
};
