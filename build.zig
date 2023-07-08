const std = @import("std");
const Builder = std.build.Builder;

pub fn build(b: *Builder) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseSafe,
    });
    const libgroove_optimize_mode = b.option(
        std.builtin.OptimizeMode,
        "libgroove-optimize",
        "override optimization mode of libgroove and its dependencies",
    );

    const groove_dep = b.dependency("groove", .{
        .optimize = libgroove_optimize_mode orelse .ReleaseFast,
        .target = target,
    });

    const client = b.addSharedLibrary(.{
        .name = "client",
        .root_source_file = .{ .path = "src/client/zig/client_main.zig" },
        .optimize = switch (optimize) {
            .ReleaseFast, .ReleaseSafe => .ReleaseSmall,
            else => optimize,
        },
        .target = .{
            .cpu_arch = .wasm32,
            .os_tag = .freestanding,
        },
    });
    client.rdynamic = true;
    client.addAnonymousModule("shared", .{ .source_file = .{ .path = "src/shared/index.zig" } });
    b.installArtifact(client);

    // TODO: update the pastejs and pastehtmlcss commands to use RunStep
    // properly rather than writing directly to this installation directory
    b.installDirectory(.{
        .source_dir = "public",
        .install_dir = .lib,
        .install_subdir = "public",
    });

    const server = b.addExecutable(.{
        .name = "groovebasin",
        .root_source_file = .{ .path = "src/server/server_main.zig" },
        .target = target,
        .optimize = optimize,
    });
    server.addAnonymousModule("shared", .{ .source_file = .{ .path = "src/shared/index.zig" } });
    const server_options = b.addOptions();
    server.addOptions("build_options", server_options);
    server_options.addOptionArtifact("client_wasm_path", client);
    server.linkLibrary(groove_dep.artifact("groove"));
    b.installArtifact(server);

    const run_cmd = b.addRunArtifact(server);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);

    {
        const paste_js_exe = b.addExecutable(.{
            .name = "paste-js",
            .root_source_file = .{ .path = "tools/paste-js.zig" },
        });

        const paste_js_cmd = b.addRunArtifact(paste_js_exe);
        paste_js_cmd.addArgs(&[_][]const u8{
            "src/client/js",
            "curlydiff.js",
            "diacritics.js",
            "event_emitter.js",
            "human-size.js",
            "inherits.js",
            "keese.js",
            "main.js",
            "mess.js",
            "music-library-index.js",
            "playerclient.js",
            "socket.js",
            "uuid.js",
        });
        const paste_js_step = b.step("paste-js", "compile the js");
        paste_js_step.dependOn(&paste_js_cmd.step);

        server.step.dependOn(&paste_js_cmd.step);
    }
    {
        const exe = b.addExecutable(.{
            .name = "paste-htmlcss",
            .root_source_file = .{ .path = "tools/paste-htmlcss.zig" },
        });

        const cmd = b.addRunArtifact(exe);
        cmd.addArgs(&[_][]const u8{
            "src/client/htmlcss/index.html",
            "src/client/htmlcss/app.css",
        });
        const step = b.step("paste-htmlcss", "compile the css and html together");
        step.dependOn(&cmd.step);

        server.step.dependOn(&cmd.step);
    }
}
