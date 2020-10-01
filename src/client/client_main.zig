extern fn print(ptr: [*]const u8, len: usize) void;

pub const browser = struct {
    pub fn _print(str: []const u8) void {
        print(str.ptr, str.len);
    }
};

export fn main() void {
    browser._print("hello world");
}
