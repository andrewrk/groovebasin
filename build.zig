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

    b.installDirectory(.{
        .source_dir = .{ .path = "public" },
        .install_dir = .lib,
        .install_subdir = "public",
    });

    const server = b.addExecutable(.{
        .name = "groovebasin",
        .root_source_file = .{ .path = "src/server/server_main.zig" },
        .target = target,
        .optimize = optimize,
    });
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

        b.getInstallStep().dependOn(&b.addInstallFileWithDir(
            paste_js_cmd.addOutputFileArg("app.js"),
            .lib,
            "public/app.js",
        ).step);

        for ([_][]const u8{
            "src/client/js/curlydiff.js",
            "src/client/js/diacritics.js",
            "src/client/js/event_emitter.js",
            "src/client/js/human-size.js",
            "src/client/js/inherits.js",
            "src/client/js/keese.js",
            "src/client/js/main.js",
            "src/client/js/mess.js",
            "src/client/js/music-library-index.js",
            "src/client/js/playerclient.js",
            "src/client/js/socket.js",
            "src/client/js/randomId.js",
        }) |input_file| {
            paste_js_cmd.addFileArg(.{ .path = input_file });
        }
    }

    {
        const exe = b.addExecutable(.{
            .name = "paste-htmlcss",
            .root_source_file = .{ .path = "tools/paste-htmlcss.zig" },
        });

        const cmd = b.addRunArtifact(exe);
        cmd.addFileArg(.{ .path = "src/client/htmlcss/index.html" });
        cmd.addFileArg(.{ .path = "src/client/htmlcss/app.css" });

        b.getInstallStep().dependOn(&b.addInstallFileWithDir(
            cmd.addOutputFileArg("index.html"),
            .lib,
            "public/index.html",
        ).step);
    }
}
