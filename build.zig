const Builder = @import("std").build.Builder;

pub fn build(b: *Builder) void {
    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    const target = b.standardTargetOptions(.{});

    // Standard release options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall.
    const mode = b.standardReleaseOptions();

    const client = b.addStaticLibrary("client", "src/client/client_main.zig");
    client.setTarget(.{
        .cpu_arch = .wasm32,
        .os_tag = .freestanding,
    });

    const exe = b.addExecutable("groovebasin", "src/main.zig");
    exe.setTarget(target);
    exe.setBuildMode(mode);
    exe.addBuildOptionArtifact("client_wasm_path", client);
    exe.install();

    const run_cmd = exe.run();
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);

    {
        const paste_js_exe = b.addExecutable("paste-js", "tools/paste-js.zig");
        paste_js_exe.setTarget(target);
        paste_js_exe.setBuildMode(mode);
        paste_js_exe.install();

        const paste_js_cmd = paste_js_exe.run();
        paste_js_cmd.addArgs(&[_][]const u8{
            "src/client",
            "bootstrap_wasm.js",
            "callback.js",
            "string.js",
            "wasmExports.js",
            "websocket.js",
        });
        const paste_js_step = b.step("paste-js", "compile the js");
        paste_js_step.dependOn(&paste_js_cmd.step);

        exe.step.dependOn(&paste_js_cmd.step);
    }
}
