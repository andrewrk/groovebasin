mpd = null

results = []

render = ->
  pass_count = 0
  fail_count = 0
  for result in results
    if result.success
      pass_count += 1
    else
      fail_count += 1
  
  $("#tests").html Handlebars.templates.view
    results: results
    pass: pass_count
    fail: fail_count

result_stack = []
wrapCallback = (result, cb) ->
    result_stack.push cur_test
    cur_test = result
    runTest cb
    cur_test = result_stack.pop()

cur_test = null
lets_test = (name) ->
  cur_test =
    name: name
    success: true
  results.push cur_test

  mpd = new Mpd()
  mpd.__test = cur_test
  mpd.onError (msg) ->
    wrapCallback mpd.__test, ->
      fail "MPD error: #{msg}"

eq = (a, b) ->
  fail "#{a} != #{b}:\n\n#{printStackTrace().join("\n")}" if a != b

ok = (value) ->
  fail "#{value} is not true:\n\n#{printStackTrace().join("\n")}" if not value

fail = (msg) ->
  cur_test.success = false
  cur_test.details = msg
  throw "TestFail"

tests = [
  ->
    lets_test "connection to mpd"
    mpd.sendCommand "status", (msg) ->
      wrapCallback mpd.__test, ->
        ok /^playlist:/m.test(msg)
]

runTest = (test) ->
  try
    test()
  catch err
    if err isnt "TestFail"
      cur_test.success = false
      cur_test.details = "#{err}:\n\n#{printStackTrace({e: err}).join("\n")}"
  render()

runTests = ->
  for test in tests
    runTest(test)

$(document).ready ->
  Handlebars.registerHelper 'hash', (context, options) ->
    ret = ""
    for k,v of context
      ret += options.fn(v)
    ret

  runTests()
