wait = (delay, func) -> setTimeout func, delay
mpd = null

results = []
cur_test = null
test_index = 0

render = ->
  pass_count = 0
  fail_count = 0
  running_count = 0
  for result in results
    if result.running()
      running_count += 1
    else
      if result.success
        pass_count += 1
      else
        fail_count += 1
  
  $("#tests").html Handlebars.templates.view
    results: results
    pass: pass_count
    fail: fail_count
    running: running_count

nonce = 0
mpd_stack = []
mpdEvent = (expect_calls, event, args..., cb) ->
  active_test = cur_test
  thread_name = "#{event}#{nonce}"
  nonce += 1
  active_test.threads[thread_name] ||= 0
  active_test.threads[thread_name] += parseInt(expect_calls)
  active_mpd = mpd
  mpd[event] args..., (cb_args...) ->
    active_test.threads[thread_name] -= 1
    return if active_test.threads[thread_name] < 0
    temp = [cur_test, mpd]
    cur_test = active_test
    mpd = active_mpd
    runTest cb, cb_args...
    [cur_test, mpd] = temp
    render()

lets_test = (name) ->
  cur_test =
    name: name
    success: true
    threads: {}
    running: ->
      return false if not this.success
      sum = 0
      sum += count for _, count of this.threads
      sum > 0
    status: ->
      if this.success
        if this.running()
          "wait"
        else
          "pass"
      else
        "fail"

  results.push cur_test

  mpd.removeEventListeners 'onError'
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
    lets_test "calling updateArtistInfo for nonexistent artist"
    mpdEvent 0, 'onLibraryUpdate', ->
      fail "unwarranted library update"
    mpd.updateArtistInfo "this artist does not exist!! aoeuaoeuaoeu"
    eq mpd.library.artist_list.length, 0
  ->
    lets_test "remove event listeners"
    count = 0
    mpdEvent 1, 'onStatusUpdate', ->
      count += 1
      ok count < 2
      mpd.removeEventListeners 'onStatusUpdate'
      mpdEvent 1, 'onStatusUpdate', ->
        ok true
      mpd.updateStatus()
    mpd.updateStatus()
  ->
    lets_test "clear playlist 1"
    mpdEvent 1, 'onPlaylistUpdate', ->
      mpd.removeEventListeners 'onPlaylistUpdate'
      mpdEvent 1, 'onPlaylistUpdate', ->
        eq mpd.playlist.item_list.length, 0
      mpd.clear()
  ->
    lets_test "get artist list"
    mpdEvent 1, 'onLibraryUpdate', ->
      ok mpd.library.artist_list.length > 1
      mpd.removeEventListeners 'onLibraryUpdate'
 
      rand_index = Math.floor(Math.random()*mpd.library.artist_list.length)
      random_artist = mpd.library.artist_list[rand_index]
      eq mpd.library.artist_table[random_artist.name], random_artist

      lets_test "get songs from artist '#{random_artist.name}'"
      album_tracks = []
      mpdEvent 1, 'onLibraryUpdate', ->
        mpd.removeEventListeners 'onLibraryUpdate'
        ok mpd.library.artist_list.length > 1
        for album_name, album of random_artist.albums
          eq album_name, album.name
          for file, track of album.tracks
            eq track.file, track.file
            ok track.name?
            eq track.artist, random_artist
            eq track.album, album

            # save for next test
            album_tracks.push track

        lets_test "clear playlist 2"
        tracks_to_add = album_tracks[0..2]
        mpdEvent 1, 'onPlaylistUpdate', ->
          mpd.removeEventListeners 'onPlaylistUpdate'
          eq mpd.playlist.item_list.length, 0

          lets_test "add tracks such as '#{tracks_to_add[0].name}' to playlist"
          count = 0
          mpdEvent tracks_to_add.length, 'onPlaylistUpdate', ->
            count += 1
            eq mpd.playlist.item_list.length, count
            eq tracks_to_add[count-1].name, mpd.playlist.item_list[count-1].track.name
            eq tracks_to_add[count-1].file, mpd.playlist.item_list[count-1].track.file
            eq tracks_to_add[count-1].time, mpd.playlist.item_list[count-1].track.time
            eq tracks_to_add[count-1].artist, mpd.playlist.item_list[count-1].track.artist

            if count < tracks_to_add.length
              mpd.queueFile tracks_to_add[count].file
              return
            
            mpd.removeEventListeners 'onPlaylistUpdate'


            lets_test "playing a track"
            id_to_play = mpd.playlist.item_list[mpd.playlist.item_list.length-1].id
            count = 0
            mpdEvent 3, 'onStatusUpdate', ->
              # call updateStatus again to flush the event queue
              mpd.updateStatus()
              return if count++ < 3
              mpd.removeEventListeners 'onStatusUpdate'

              eq mpd.status.state, "play"
              eq mpd.status.current_item.id, id_to_play

            mpd.playId id_to_play
          mpd.queueFile tracks_to_add[count].file
        mpd.clear()
      mpd.updateArtistInfo random_artist.name
  ->
    lets_test "pausing a track"
    mpdEvent 1, 'onStatusUpdate', ->
      eq mpd.status.state, "pause"

    mpd.pause()
  ->
    lets_test "unpausing a track"
    mpdEvent 1, 'onStatusUpdate', ->
      eq mpd.status.state, "play"

    mpd.play()
  ->
    lets_test "seeking current track"
    mpdEvent 1, 'onStatusUpdate', ->
      ok mpd.status.elapsed >= mpd.status.time / 2
    mpd.seek mpd.status.time / 2
  ->
    lets_test "remove item from playlist"
    mpdEvent 1, 'onPlaylistUpdate', ->
      mpd.removeEventListeners 'onPlaylistUpdate'
      old_len = mpd.playlist.item_list.length
      mpdEvent 1, 'onPlaylistUpdate', ->
        ok mpd.playlist.item_list.length, old_len - 1
      mpd.removeId mpd.playlist.item_list[0].id
  ->
    lets_test "clear playlist 3"
    mpdEvent 1, 'onPlaylistUpdate', ->
      eq mpd.playlist.item_list.length, 0

    mpd.clear()
  ->
    lets_test "get random song"
    mpdEvent 1, 'onLibraryUpdate', ->
      mpdEvent 1, 'getRandomTrack', (track) ->
        ok track.file?
  ->
    lets_test "get several random songs very quickly"
    mpdEvent 1, 'onLibraryUpdate', ->
      for i in [0..20]
        mpdEvent 1, 'getRandomTrack', (track) ->
          ok track.file?
]

runTest = (test, args...) ->
  try
    test(args...)
  catch err
    if err isnt "TestFail"
      cur_test.success = false
      cur_test.details = "#{err}:\n\n#{printStackTrace({e: err}).join("\n")}"
  if not cur_test.running()
    # test is over
    mpd.close()
    runNextTest()
  render()

runNextTest = ->
  if test_index < tests.length
    mpd = new Mpd()
    runTest(tests[test_index++])

$(document).ready ->
  Handlebars.registerHelper 'hash', (context, options) ->
    ret = ""
    for k,v of context
      ret += options.fn(v)
    ret

  runNextTest()
