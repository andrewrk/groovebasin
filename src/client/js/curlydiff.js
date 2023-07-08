module.exports.diff = diff;
module.exports.apply = apply;
module.exports.isObject = isObject;

function diff(from, to) {
  if (!isObject(from) || !isObject(to)) {
    // not both objects
    if (from === to) return undefined;
    if (from instanceof Date && to instanceof Date && from.getTime() === to.getTime()) return undefined;
    // there's a difference
    return to;
  }
  // both are objects
  var result = {};
  var anythingChanged = false;
  for (var key in from) {
    var childDiff;
    if (key in to) {
      childDiff = diff(from[key], to[key]);
      if (childDiff === undefined) continue;
    } else {
      // deleted
      childDiff = null;
    }
    // there's a difference
    result[key] = childDiff;
    anythingChanged = true;
  }
  for (var key in to) {
    if (key in from) continue; // handled above
    result[key] = to[key];
    anythingChanged = true;
  }
  if (anythingChanged) return result;
  // no change
  return undefined;
}

function apply(object, patch) {
  if (patch === undefined) return object;
  if (!isObject(object) || !isObject(patch)) return patch;
  // both are objects
  for (var key in patch) {
    var patchChild = patch[key];
    if (patchChild == null) {
      // removed
      delete object[key];
    } else {
      // either this assignment or this function call will have side effects
      object[key] = apply(object[key], patchChild);
    }
  }
  return object;
}

function isObject(object) {
  if (object == null) return false;
  if (typeof object !== "object") return false;
  if (Array.isArray(object)) return false;
  if (object instanceof Date) return false;
  return true;
}
