
const {HandleRegistry} = require("handleRegistry");

const elementRegistry = new HandleRegistry();
const eventRegistry = new HandleRegistry();

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
function setAttribute(handle, key, value) {
    elementRegistry.registry[handle].setAttribute(key, value);
}
function getAttribute(handle, key) {
    return elementRegistry.registry[handle].getAttribute(key);
}

// Searches the start_handle node and its .parentNode ancestors for a node whose .classList.contains(class_).
// If not found, returns stop_handle.
function searchAncestorsForClass(start_handle, stop_handle, class_) {
    let node = elementRegistry.registry[start_handle];
    if (node.classList.contains(class_)) return start_handle;
    const root = elementRegistry.registry[stop_handle];
    if (!root.contains(node)) return stop_handle;
    if (node === root) return stop_handle;

    while (true) {
        node = node.parentNode;
        if (node === root) return stop_handle;
        if (node.classList.contains(class_)) {
            const {handle} = elementRegistry.alloc(node);
            return handle;
        }
    }
}

function addEventListener(handle, event_type, cb) {
    elementRegistry.registry[handle].addEventListener(event_type, function(event) {
        const {handle, dispose} = eventRegistry.alloc(event);
        try {
            cb(handle);
        } finally {
            dispose();
        }
    });
}

function getEventTarget(event_handle) {
    const event = eventRegistry.registry[event_handle];
    const target = event.target;
    const {handle} = elementRegistry.alloc(target);
    return handle;
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
    setAttribute,
    getAttribute,
    searchAncestorsForClass,
    addEventListener,
    getEventTarget,
};
