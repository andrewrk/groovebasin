// This is a mechanism for transferring strings/blobs from JS to Wasm.
// The pattern is:
// * JS has a UInt8Array blob to give to Wasm.
// * JS: call createBlob() which allocates a handle pointing to the array.
// * JS: pass (blob.handle, blob.length) to Wasm.
//   * Wasm: allocate the necessary buffer to receive a copy of blob in Wasm memory.
//   * Wasm: call readBlob(handle, buffer.ptr, buffer.len).
//     * JS: assert the correct length, and copy the array contents into Wasm memory at the specified pointer.
//   * Wasm: return
// * JS: call blob.dispose(), which makes the handle invalid.

const {HandleRegistry} = require("handleRegistry");
const blobRegistry = new HandleRegistry();

// See above comment.
// array should be a UInt8Array.
function createBlob(array) {
    return blobRegistry.alloc(array);
}

// This gets called with a view into Wasm memory.
function readBlob(handle, dest) {
    const array = blobRegistry.registry[handle];
    if (array == null) throw new Error("bad blob handle");
    if (array.length !== dest.length) throw new Error("wrong buffer length for reading blob");
    dest.set(array);
}

return {
    createBlob,
    readBlob,
};
