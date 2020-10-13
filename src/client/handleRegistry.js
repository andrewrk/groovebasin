
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
                delete this.registry[handle];
            },
        }
    }
}

return {HandleRegistry};
