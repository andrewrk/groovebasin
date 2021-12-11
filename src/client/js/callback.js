const wasmExports = require("wasmExports");

function wrapCallback(cb) {
    return () => wasmExports.delegateCallback(cb);
}
function wrapCallbackI32(cb) {
    return (arg) => wasmExports.delegateCallbackI32(cb, arg);
}
function wrapCallbackSliceU8(cb) {
    return (arg1, arg2) => wasmExports.delegateCallbackSliceU8(cb, arg1, arg2);
}
function wrapCallbackI32RI32(cb) {
    return (arg) => wasmExports.delegateCallbackI32RI32(cb, arg);
}

return {
    wrapCallback,
    wrapCallbackI32,
    wrapCallbackSliceU8,
    wrapCallbackI32RI32,
};
