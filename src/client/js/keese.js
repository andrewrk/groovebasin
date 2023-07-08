// the basic characters in sorted order
var alphabet = "0123456789?@ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
var radix = alphabet.length;
// bigger than all the basic characters
var order_specifier = "~";

// character to numerical value aka index of the character
// "0": 0, "z": 63, etc.
var values = (function() {
  var values = {};
  for (var i = 0; i < alphabet.length; i++) values[alphabet[i]] = i;
  return values;
})();

module.exports = keese;
function keese(low, high, count) {
  if (count != null) {
    return multi_keese(low, high, count);
  } else {
    return single_keese(low, high);
  }
}
function single_keese(low, high) {
  if (low == null) {
    if (high == null) {
      // return anything above 0
      return "1";
    } else {
      // go smaller
      return average("0", high);
    }
  } else {
    if (high == null) {
      // go bigger
      return increment(low);
    } else {
      // go in between
      return average(low, high);
    }
  }
}
function multi_keese(low, high, count) {
  var result = new Array(count);
  if (count > 0) {
    if (high == null) {
      // just allocate straight forward
      for (var i = 0; i < count; i++) {
        var value = keese(low, null);
        result[i] = value;
        low = value;
      }
    } else {
      // binary tree descent
      recurse(low, high, 0, count);
    }
  }
  return result;
  function recurse(low_value, high_value, low_index, high_index) {
    var mid_index = Math.floor((low_index + high_index) / 2);
    var mid_value = single_keese(low_value, high_value);
    result[mid_index] = mid_value;
    if (low_index < mid_index) recurse(low_value, mid_value, low_index, mid_index);
    if (mid_index + 1 < high_index) recurse(mid_value, high_value, mid_index + 1, high_index);
  }
}

function increment(value) {
  var n = parse(value);
  // drop the fraction
  n.digits = n.digits.substr(0, n.order_length + 1);
  return add(n, parse("1"));
}

function average(low, high) {
  if (!(low < high)) {
    throw new Error("assertion failed: " + JSON.stringify(low) + " < " + JSON.stringify(high));
  }
  var a = parse(low);
  var b = parse(high);
  pad_to_equal_order(a, b);
  var b_carry = 0;
  var max_digit_length = Math.max(a.digits.length, b.digits.length);
  for (var i = 0; i < max_digit_length || b_carry > 0; i++) {
    var a_value =            values[a.digits[i]] || 0;
    var b_value = b_carry + (values[b.digits[i]] || 0);
    if (a_value === b_value) continue;
    if (a_value === b_value - 1) {
      // we need more digits, but remember that b is ahead
      b_carry = radix;
      continue;
    }
    // we have a distance of at least 2 between the values.
    // half the distance floored is sure to be a positive single digit.
    var half_distance_value = Math.floor((b_value - a_value) / 2);
    var half_distance_digits = "";
    for (var j = 0; j < i; j++)
      half_distance_digits += "0";
    half_distance_digits += alphabet[half_distance_value];
    var half_distance = parse(construct(a.order_length, half_distance_digits));
    // truncate insignificant digits of a
    a.digits = a.digits.substr(0, i + 1);
    return add(a, half_distance);
  }
  throw new Error; // unreachable
}

function add(a, b) {
  pad_to_equal_order(a, b);
  var result_digits = "";
  var order_length = a.order_length;
  var value = 0;
  for (var i = Math.max(a.digits.length, b.digits.length) - 1; i >= 0; i--) {
    value += values[a.digits[i]] || 0;
    value += values[b.digits[i]] || 0;
    result_digits = alphabet[value % radix] + result_digits;
    value = Math.floor(value / radix);
  }
  // overflow up to moar digits
  while (value > 0) {
    result_digits = alphabet[value % radix] + result_digits;
    value = Math.floor(value / radix);
    order_length++;
  }
  return construct(order_length, result_digits);
}

function parse(value) {
  var order_length = value.lastIndexOf(order_specifier) + 1;
  return {
    order_length: order_length,
    digits: value.substr(order_length)
  };
}
function construct(order_length, digits) {
  // strip unnecessary leading zeros
  while (order_length > 0 && digits.charAt(0) == "0") {
    digits = digits.substr(1);
    order_length--;
  }
  var result = "";
  for (var i = 0; i < order_length; i++)
    result += order_specifier;
  return result + digits;
}

function pad_to_equal_order(a, b) {
  pad_in_place(a, b.order_length);
  pad_in_place(b, a.order_length);
}
function pad_in_place(n, order_length) {
  while (n.order_length < order_length) {
    n.digits = "0" + n.digits;
    n.order_length++;
  }
}
