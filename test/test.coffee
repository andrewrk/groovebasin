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

cur_test = null
lets_test = (name) ->
  cur_test =
    name: name
    success: true
  results.push cur_test

  mpd?.close()
  mpd = new Mpd()
  mpd.onError (msg) ->
    runTest ->
      fail "MPD error: #{msg}"

eq = (a, b) ->
  fail "#{a} != #{b}" if a != b

fail = (msg) ->
  cur_test.success = false
  cur_test.details = msg
  throw "TestFail"

tests = [
  ->
    lets_test "dummy fail"
    fail "always fails"
  ->
    lets_test "dummy pass"
    eq true, true
  ->
    lets_test "test with a runtime error"
    a = {}
    a.foo.blah = true
    eq true, true
  ->
    lets_test "mpd having an error"
    mpd.sendCommand "help"
]

runTest = (test) ->
  try
    test()
  catch err
    if err isnt "TestFail"
      cur_test.success = false
      cur_test.details = "#{err}:<br> #{printStackTrace({e: err}).join("<br>")}"
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

  # debug text box
  $("#line").keydown (event) ->
    if event.keyCode == 13
      line = $("#line").val()
      $("#line").val('')
      mpd.sendCommand line, (msg) ->
        $("#text").val(msg)

  runTests()
