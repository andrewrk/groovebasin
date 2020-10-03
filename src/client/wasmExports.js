// This object collects all the instance.exports once wasm has been instantiated.
// A few exports are excluded, such as main().
// Search the .zig code for `export` to find most of this object.
// Also exposes some builtin stuff, like .__heap_base, whatever that is.
return {};
