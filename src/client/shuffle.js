// adapted from http://stackoverflow.com/questions/6274339/how-can-i-shuffle-an-array-in-javascript#6274398
function shuffle(array) {
    // Iterate backwards picking a random element to put into each slot.
    var counter = array.length;
    while (counter > 0) {
        var index = Math.floor(Math.random() * counter);
        counter--;

        var temp = array[counter];
        array[counter] = array[index];
        array[index] = temp;
    }
}

return shuffle;
