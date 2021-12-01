const wasmExports = require("wasmExports");

function wrapCallback(callbackPtr, context) {
    return () => wasmExports.delegateCallback(callbackPtr, context);
}
function wrapCallbackI32(callbackPtr, context) {
    return (arg) => wasmExports.delegateCallbackI32(callbackPtr, context, arg);
}
function wrapCallbackSliceU8(callbackPtr, context) {
    return (arg1, arg2) => wasmExports.delegateCallbackSliceU8(callbackPtr, context, arg1, arg2);
}
function wrapCallbackI32RI32(callbackPtr, context) {
    return (arg) => wasmExports.delegateCallbackI32RI32(callbackPtr, context, arg);
}

return {
    wrapCallback,
    wrapCallbackI32,
    wrapCallbackSliceU8,
    wrapCallbackI32RI32,
};
