const wasmExports = require("wasmExports");

function wrapCallback(callbackPtr, context) {
    return () => wasmExports.delegateCallback(callbackPtr, context);
}
function wrapCallbackI32(callbackPtr, context) {
    return (arg) => wasmExports.delegateCallbackI32(callbackPtr, context, arg);
}

return {
    wrapCallback,
    wrapCallbackI32,
};
