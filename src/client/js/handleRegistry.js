
class HandleRegistry {
    constructor() {
        this.registry = {};
        this.nextHandle = 0;
    }
    alloc(obj) {
        const handle = this.nextHandle;
        this.nextHandle = 0x7fffffff & (this.nextHandle + 1);
        this.registry[handle] = obj;
        return {
            handle,
            dispose: () => {
                this.disposeHandle(handle);
            },
        }
    }
    disposeHandle(handle) {
        if (!(handle in this.registry)) {
            console.log("WARNING: disposing non-existent handle: " + handle);
        }
        delete this.registry[handle];
    }
}

return {HandleRegistry};
