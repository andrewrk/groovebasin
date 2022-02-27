const std = @import("std");
const Allocator = std.mem.Allocator;

const ws = @import("websocket_handler.zig");
const groovebasin_ui = @import("groovebasin_ui.zig");
const g = @import("global.zig");

const Library = @import("shared").Library;
const Queue = @import("shared").Queue;
const Events = @import("shared").Events;

const protocol = @import("shared").protocol;
const Track = protocol.Track;
const QueueItem = protocol.QueueItem;
const Event = protocol.Event;
const StringPool = @import("shared").StringPool;

var last_library_version: u64 = 0;
var library: Library = undefined;

var last_queue_version: u64 = 0;
var queue: Queue = undefined;

var last_events_version: u64 = 0;
var events: Events = undefined;

pub fn init() void {
    library = Library.init(g.gpa);
    queue = Queue.init(g.gpa);
    events = Events.init(g.gpa);
}

pub fn poll() !void {
    var query_call = try ws.Call.init(.query);
    try query_call.writer().writeStruct(protocol.QueryRequest{
        .last_library = last_library_version,
        .last_queue = last_queue_version,
        .last_events = last_events_version,
    });
    try query_call.send(handleQueryResponse, {});
}
fn handleQueryResponse(response: []const u8) anyerror!void {
    var arena_instance = std.heap.ArenaAllocator.init(g.gpa);
    defer arena_instance.deinit();
    var arena = arena_instance.allocator();

    var stream = std.io.fixedBufferStream(response);
    const response_header = try stream.reader().readStruct(protocol.QueryResponseHeader);
    var render_library = false;
    var render_queue = false;
    var render_events = false;

    // Library
    if (response_header.library_version != last_library_version) {
        const library_header = try stream.reader().readStruct(protocol.LibraryHeader);
        // string pool
        var strings = try StringPool.initSizeImmutable(arena, library_header.string_size);
        try stream.reader().readNoEof(strings.bytes.items);
        // track keys and values
        library.tracks.clearRetainingCapacity();
        try library.tracks.ensureTotalCapacity(library_header.track_count);
        var key_stream = std.io.fixedBufferStream(response[stream.pos..]);
        var value_stream = std.io.fixedBufferStream(response[stream.pos + @sizeOf(u64) * library_header.track_count ..]);

        var i: u32 = 0;
        while (i < library_header.track_count) : (i += 1) {
            try library.putTrack(
                strings,
                try key_stream.reader().readIntLittle(u64),
                try value_stream.reader().readStruct(Track),
            );
        }
        stream.pos += library_header.track_count * @sizeOf(u64) + library_header.track_count * @sizeOf(Track);

        last_library_version = response_header.library_version;
        render_library = true;
    }

    // Queue
    if (response_header.queue_version != last_queue_version) {
        const queue_header = try stream.reader().readStruct(protocol.QueueHeader);
        // item keys and values
        queue.items.clearRetainingCapacity();
        // try queue.items.ensureTotalCapacity(queue_header.item_count);
        var key_stream = std.io.fixedBufferStream(response[stream.pos..]);
        var value_stream = std.io.fixedBufferStream(response[stream.pos + @sizeOf(u64) * queue_header.item_count ..]);

        var i: u32 = 0;
        while (i < queue_header.item_count) : (i += 1) {
            try queue.items.putNoClobber(
                try key_stream.reader().readIntLittle(u64),
                try value_stream.reader().readStruct(QueueItem),
            );
        }
        stream.pos += queue_header.item_count * @sizeOf(u64) + queue_header.item_count * @sizeOf(QueueItem);

        last_queue_version = response_header.queue_version;
        render_queue = true;
    }

    // Events
    if (response_header.events_version != last_events_version) {
        const events_header = try stream.reader().readStruct(protocol.EventsHeader);
        // string pool
        var strings = try StringPool.initSizeImmutable(arena, events_header.string_size);
        try stream.reader().readNoEof(strings.bytes.items);
        // keys and values
        events.events.clearRetainingCapacity();
        try events.events.ensureTotalCapacity(events_header.item_count);
        var key_stream = std.io.fixedBufferStream(response[stream.pos..]);
        var value_stream = std.io.fixedBufferStream(response[stream.pos + @sizeOf(u64) * events_header.item_count ..]);

        var i: u32 = 0;
        while (i < events_header.item_count) : (i += 1) {
            try events.putEvent(
                strings,
                try key_stream.reader().readIntLittle(u64),
                try value_stream.reader().readStruct(Event),
            );
        }
        stream.pos += events_header.item_count * @sizeOf(u64) + events_header.item_count * @sizeOf(Event);

        last_events_version = response_header.events_version;
        render_events = true;
    }

    if (render_library) groovebasin_ui.renderLibrary();
    if (render_queue) groovebasin_ui.renderQueue();
    if (render_events) groovebasin_ui.renderEvents();
}
