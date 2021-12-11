
const {HandleRegistry} = require("handleRegistry");
const {KeyboardEventCode_inverse} = require("enums");

const elementRegistry = new HandleRegistry();
const eventRegistry = new HandleRegistry();

function getElementByHandle(handle) {
    let element = elementRegistry.registry[handle];
    if (element != null) {
        return element;
    }
    element = document.getElementById("h-" + handle.toString(16));
    if (element != null) {
        return element;
    }
    throw new Error("bogus element handle: 0x" + handle.toString(16));
}

function getElementHandle(element) {
    let handle_str = element.getAttribute("data-h");
    if (handle_str != null) {
        return parseInt(handle_str, 16);
    }
    if (element.getAttribute("id") != null || !document.contains(element)) {
        // using document.getElementById("handle-" + handle) isn't going to work.
        // Use the registry to find this element later.
        const {handle} = elementRegistry.alloc(element);
        handle_str = handle.toString(16);
        element.setAttribute("data-h", handle_str);
        return handle;
    } else {
        // Give the element an id to use to find it later.
        // This avoids pointers to deleted elements preventing them from being garbaged collected.
        const handle = elementRegistry.nextHandle();
        handle_str = handle.toString(16);
        element.setAttribute("id", "h-" + handle_str);
        element.setAttribute("data-h", handle_str);
        return handle;
    }
}

function getElementById(id) {
    const element = document.getElementById(id);
    if (element == null) throw new Error("no such element: " + id);
    return getElementHandle(element);
}

function setElementShown(handle, shown) {
    getElementByHandle(handle).style.display = shown ? "" : "none";
}

function setElementTextContent(handle, text) {
    getElementByHandle(handle).textContent = text;
}

function getChildrenCount(handle) {
    return getElementByHandle(handle).children.length;
}

function getChild(parent_handle, i) {
    const parent = getElementByHandle(parent_handle);
    const child = parent.children[i];
    if (child == null) throw new Error("bad child index: " + i);
    return getElementHandle(child);
}

function insertAdjacentHTML(handle, position, html) {
    getElementByHandle(handle).insertAdjacentHTML(position, html);
}

function removeLastChild(handle) {
    const element = getElementByHandle(handle);
    element.removeChild(element.lastChild);
}

function addClass(handle, class_) {
    getElementByHandle(handle).classList.add(class_);
}
function removeClass(handle, class_) {
    getElementByHandle(handle).classList.remove(class_);
}
function setAttribute(handle, key, value) {
    getElementByHandle(handle).setAttribute(key, value);
}
function getAttribute(handle, key) {
    return getElementByHandle(handle).getAttribute(key);
}

// Searches the start_handle node and its .parentNode ancestors for a node whose .classList.contains(class_).
// If not found, returns stop_handle.
function searchAncestorsForClass(start_handle, stop_handle, class_) {
    let node = getElementByHandle(start_handle);
    if (node.classList.contains(class_)) return start_handle;
    const root = getElementByHandle(stop_handle);
    if (!root.contains(node)) return stop_handle;
    if (node === root) return stop_handle;

    while (true) {
        node = node.parentNode;
        if (node === root) return stop_handle;
        if (node.classList.contains(class_)) {
            return getElementHandle(node);
        }
    }
}

function addEventListener(handle, event_type, cb) {
    getElementByHandle(handle).addEventListener(event_type, function(event) {
        const {handle, dispose} = eventRegistry.alloc(event);
        try {
            cb(handle);
        } finally {
            dispose();
        }
    });
}

function addWindowEventListener(event_type, cb) {
    window.addEventListener(event_type, function(event) {
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
    return getElementHandle(target);
}
function getEventModifiers(event_handle) {
    const event = eventRegistry.registry[event_handle];
    return (
        (event.shiftKey ? (1 << 0) : 0) |
        (event.ctrlKey ? (1 << 1) : 0) |
        (event.altKey ? (1 << 2) : 0) |
        (event.metaKey ? (1 << 3) : 0)
    );
}
function getKeyboardEventCode(event_handle) {
    const event = eventRegistry.registry[event_handle];
    const code_code = KeyboardEventCode_inverse[event.code];
    if (typeof code_code !== "number") throw new Error("event.code not recognized: " + event.code);
    return code_code;
}
function preventDefault(event_handle) {
    const event = eventRegistry.registry[event_handle];
    event.preventDefault();
}

function setInputValueAsNumber(handle, value) {
    getElementByHandle(handle).valueAsNumber = value;
}
function getInputValueAsNumber(handle) {
    return getElementByHandle(handle).valueAsNumber;
}

return {
    getElementByHandle,
    getElementHandle,
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
    addWindowEventListener,
    getEventTarget,
    getEventModifiers,
    getKeyboardEventCode,
    preventDefault,
    setInputValueAsNumber,
    getInputValueAsNumber,
};
