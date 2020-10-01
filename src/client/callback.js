
function wrapCallback(id, context) {
    return () => exports.delegateCallback(id, context);
}
function wrapCallbackI32(id, context) {
    return (arg) => exports.delegateCallbackI32(id, context, arg);
}

const exports = {
    delegateCallback: null,
    delegateCallbackI32: null,

    wrapCallback,
    wrapCallbackI32,
};

return exports;
