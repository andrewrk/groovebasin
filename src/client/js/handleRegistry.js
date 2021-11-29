
class HandleRegistry {
    constructor() {
        this.registry = {};
        this._nextHandle = 0;
    }
    nextHandle() {
        while (true) {
            let handle = this._nextHandle;
            this._nextHandle = 0x7fffffff & (this._nextHandle + 1);
            if (!(handle in this.registry)) return handle;
        }
    }
    alloc(obj) {
        const handle = this.nextHandle();
        this.registry[handle] = obj;
        return {
            handle,
            dispose: () => {
                if (!(handle in this.registry)) {
                    console.log("WARNING: disposing non-existent handle: " + handle);
                }
                delete this.registry[handle];
            },
        }
    }
}

return {HandleRegistry};
