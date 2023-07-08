(function(module) {
    // see https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
    // and http://stackoverflow.com/questions/6274339/how-can-i-shuffle-an-array-in-javascript#6274398
    module.exports = function shuffle(array) {
        var counter = array.length,
            temp,
            index;

        while (counter) {
            index = Math.floor(Math.random() * counter--);

            temp = array[counter];
            array[counter] = array[index];
            array[index] = temp;
        }

        return array;
    }
})(module);
