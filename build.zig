const std = @import("std");
const Builder = std.build.Builder;

pub fn build(b: *Builder) void {
    const target = b.standardTargetOptions(.{});
    const mode = b.standardReleaseOptions();

    const groove_dep = b.dependency("groove", .{
        // TODO: do this after zig implements passing build options to dependencies
        //.mode = .ReleaseFast,
    });

    const client = b.addSharedLibrary("client", "src/client/zig/client_main.zig", .unversioned);
    client.rdynamic = true;
    client.setBuildMode(switch (mode) {
        .ReleaseFast => .ReleaseSmall,
        else => mode,
    });
    client.setTarget(.{
        .cpu_arch = .wasm32,
        .os_tag = .freestanding,
    });
    client.addPackagePath("shared", "src/shared/index.zig");
    client.install();

    // TODO: watch out for race conditions between this install and the paste-*
    // commands generating content here.
    b.installDirectory(.{
        .source_dir = "public",
        .install_dir = .lib,
        .install_subdir = "public",
    });

    const server = b.addExecutable("groovebasin", "src/server/server_main.zig");
    server.setTarget(target);
    server.setBuildMode(mode);
    server.addPackagePath("shared", "src/shared/index.zig");
    const server_options = b.addOptions();
    server.addOptions("build_options", server_options);
    server_options.addOptionArtifact("client_wasm_path", client);
    server.linkLibrary(groove_dep.artifact("groove"));
    server.install();

    const run_cmd = server.run();
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
            "src/client/js",
            "_generated_EventType.js",
            "_generated_KeyboardEventCode.js",
            "audio.js",
            "bootstrap_wasm.js",
            "callback.js",
            "dom.js",
            "enums.js",
            "handleRegistry.js",
            "string.js",
            "wasmExports.js",
            "websocket.js",
        });
        const paste_js_step = b.step("paste-js", "compile the js");
        paste_js_step.dependOn(&paste_js_cmd.step);

        server.step.dependOn(&paste_js_cmd.step);
    }
    {
        const exe = b.addExecutable("paste-htmlcss", "tools/paste-htmlcss.zig");
        exe.setTarget(target);
        exe.setBuildMode(mode);
        exe.install();

        const cmd = exe.run();
        cmd.addArgs(&[_][]const u8{
            "src/client/htmlcss/index.html",
            "src/client/htmlcss/app.css",
        });
        const step = b.step("paste-htmlcss", "compile the css and html together");
        step.dependOn(&cmd.step);

        server.step.dependOn(&cmd.step);
    }
}
