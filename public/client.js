function print(line) {
    var txt = $("#out");
    txt.val(txt.val() + line + "\n");
}

WEB_SOCKET_SWF_LOCATION = "/public/vendor/socket.io/WebSocketMain.swf";
var socket = io.connect("http://localhost");
socket.on('FromMpd', function(data) {
    print("[in] " + data);
});

$(document).ready(function() {
    $("#line").keydown(function(event) {
        if (event.keyCode == 13) {
            var line = $("#line").val();
            $("#line").val('');
            socket.emit('ToMpd', line + "\n");
        }
    });
});
