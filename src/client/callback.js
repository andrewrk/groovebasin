const wasmExports = require("wasmExports");

function wrapCallback(callbackPtr, context) {
    return () => wasmExports.delegateCallback(callbackPtr, context);
}
function wrapCallbackI32(callbackPtr, context) {
    return (arg) => wasmExports.delegateCallbackI32(callbackPtr, context, arg);
}
function wrapCallbackI32I32(callbackPtr, context) {
    return (arg1, arg2) => wasmExports.delegateCallbackI32I32(callbackPtr, context, arg1, arg2);
}

return {
    wrapCallback,
    wrapCallbackI32,
    wrapCallbackI32I32,
};
