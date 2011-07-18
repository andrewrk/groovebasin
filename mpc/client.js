function print(line) {
    var txt = $("#out");
    txt.val(txt.val() + line + "\n");
}

$(document).ready(function() {
    var socket = new WebSocket("ws://localhost:6601/");
    socket.onopen = function() {
        print("socket onopen");
    };
    socket.onmessage = function(msg){
        print("got msg: " + msg.data);
    };
    socket.onclose = function() {
        print("socket onclose");
    };
    socket.onerror = function(error) {
        print("socket onerror: " + error);
    };

    $("#send").click(function() {
        var line = $("#line").val();
        $("#line").val('');
        socket.send(line);
    });
});
