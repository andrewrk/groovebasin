
function wrapCallback(callbackPtr, context) {
    return () => exports.delegateCallback(callbackPtr, context);
}
function wrapCallbackI32(callbackPtr, context) {
    return (arg) => exports.delegateCallbackI32(callbackPtr, context, arg);
}

const exports = {
    delegateCallback: null,
    delegateCallbackI32: null,

    wrapCallback,
    wrapCallbackI32,
};

return exports;
