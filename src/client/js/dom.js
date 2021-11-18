
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

function getChildrenCount(handle) {
    return elementRegistry.registry[handle].children.length;
}

function getChild(parent_handle, i) {
    const parent = elementRegistry.registry[parent_handle];
    const child = parent.children[i];
    if (child == null) throw new Error("bad child index: " + i);
    const {handle} = elementRegistry.alloc(child);
    return handle;
}

function insertAdjacentHTML(handle, position, html) {
    elementRegistry.registry[handle].insertAdjacentHTML(position, html);
}

function removeLastChild(handle) {
    const element = elementRegistry.registry[handle];
    element.removeChild(element.lastChild);
}

function addClass(handle, class_) {
    elementRegistry.registry[handle].classList.add(class_);
}
function removeClass(handle, class_) {
    elementRegistry.registry[handle].classList.remove(class_);
}

return {
    getElementById,
    releaseElementHandle,
    setElementShown,
    setElementTextContent,
    getChildrenCount,
    getChild,
    insertAdjacentHTML,
    removeLastChild,
    addClass,
    removeClass,
};
