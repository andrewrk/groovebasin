mpd = null

results = []
cur_test = null

render = ->
  pass_count = 0
  fail_count = 0
  running_count = 0
  for result in results
    if result.success
      pass_count += 1
    else
      fail_count += 1
    running_count += 1 if result.running()
  
  $("#tests").html Handlebars.templates.view
    results: results
    pass: pass_count
    fail: fail_count
    running: running_count

result_stack = []
mpdEvent = (expect_calls, event, args..., cb) ->
  active_test = cur_test
  active_test.threads += expect_calls
  mpd[event] args..., (cb_args...) ->
    result_stack.push cur_test
    cur_test = active_test
    runTest cb, cb_args...
    cur_test = result_stack.pop()
    active_test.threads -= expect_calls
    render()

lets_test = (name) ->
  cur_test =
    name: name
    success: true
    threads: 0
    running: -> this.threads > 0
    status: ->
      if this.success
        if this.running()
          "wait"
        else
          "pass"
      else
        "fail"

  results.push cur_test

  mpd = new Mpd()
  mpdEvent 0, 'onError', (msg) ->
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
    mpdEvent 1, 'sendCommand', "status", (msg) ->
      ok /^playlist:/m.test(msg)
      ok /^repeat:/m.test(msg)
      ok /^random:/m.test(msg)
  ->
    lets_test "two instances of mpd"
    mpdEvent 1, 'sendCommand', "status", (msg) ->
      ok /^playlist:/m.test(msg)
      ok /^repeat:/m.test(msg)
      ok /^random:/m.test(msg)
  ->
    lets_test "get artist list"
    mpdEvent 1, 'onLibraryUpdate', ->
      ok mpd.library.artist_list.length > 1
    mpd.updateArtistList()
]

runTest = (test, args...) ->
  try
    test(args...)
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
