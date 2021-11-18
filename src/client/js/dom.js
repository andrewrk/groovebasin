
const {HandleRegistry} = require("handleRegistry");

const elementRegistry = new HandleRegistry();

setInterval(runGarbageCollector, 30000);

function runGarbageCollector() {
    let kept = 0;
    let purged = 0;
    for (let handle in elementRegistry.registry) {
        const element = elementRegistry.registry[handle];
        if (window.document.contains(element)) {
            kept++;
        } else {
            delete elementRegistry.registry[handle];
            purged++;
        }
    }
    console.log("dom gc: k:" + kept + " p:" + purged);
}

function getElementById(id) {
    const element = document.getElementById(id);
    if (element == null) throw new Error("no such element: " + id);
    const {handle} = elementRegistry.alloc(element);
    return handle;
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
    setElementShown,
    setElementTextContent,
    getChildrenCount,
    getChild,
    insertAdjacentHTML,
    removeLastChild,
    addClass,
    removeClass,
};
