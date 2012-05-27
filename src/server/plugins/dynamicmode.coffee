Plugin = require('../plugin').Plugin
mpd = require '../mpd'


LAST_QUEUED_STICKER = "groovebasin.last-queued"
exports.Plugin = class DynamicMode extends Plugin
  constructor: ->
    super()
    @previous_ids = {}
    @is_enabled = false
    @got_stickers = false

  restoreState: (state) =>
    @is_on = state.status.dynamic_mode ? false
    @random_ids = state.status.random_ids ? {}
    @history_size = state.status.dynamic_history ? 10
    @future_size = state.status.dynamic_future ? 10

  saveState: (state) =>
    state.status.dynamic_mode = @is_on
    state.status.dynamic_mode_enabled = @is_enabled
    state.status.random_ids = @random_ids
    state.status.dynamic_history = @history_size
    state.status.dynamic_future = @future_size

  setConf: (conf, conf_path) =>
    @is_enabled = true
    unless conf.sticker_file?
      @is_enabled = false
      @is_on = false
      @log.warn "sticker_file not set in #{conf_path}. Dynamic Mode disabled."

  setMpd: (@mpd) =>
    @mpd.on 'statusupdate', @checkDynamicMode
    @mpd.on 'playlistupdate', @checkDynamicMode
    @mpd.on 'libraryupdate', @updateStickers

  onSocketConnection: (socket) =>
    socket.on 'DynamicMode', (data) =>
      return unless @is_enabled
      value = JSON.parse data.toString()
      return if @is_on == value
      @log.debug "DynamicMode is being turned #{value}"
      @is_on = value
      @checkDynamicMode()
      @onStatusChanged()

  checkDynamicMode: =>
    return unless @is_enabled
    return unless @mpd.library.artists.length
    return unless @got_stickers
    item_list = @mpd.playlist.item_list
    current_id = @mpd.status.current_item?.id
    current_index = -1
    all_ids = {}
    new_files = []
    for item, i in item_list
      if item.id == current_id
        current_index = i
      all_ids[item.id] = true
      new_files.push item.track.file unless @previous_ids[item.id]?
    # tag any newly queued tracks
    @mpd.sendCommands ("sticker set song \"#{file}\" \"#{LAST_QUEUED_STICKER}\" #{JSON.stringify new Date()}" for file in new_files)
    # anticipate the changes
    @mpd.library.track_table[file].last_queued = new Date() for file in new_files
    # if no track is playing, assume the first track is about to be
    if current_index == -1
      current_index = 0
    else
      # any tracks <= current track don't count as random anymore
      for i in [0..current_index]
        delete @random_ids[item_list[i].id]

    if @is_on
      commands = []
      delete_count = Math.max(current_index - @history_size, 0)
      for i in [0...delete_count]
        commands.push "deleteid #{item_list[i].id}"
      add_count = Math.max(@future_size + 1 - (item_list.length - current_index), 0)

      commands = commands.concat ("addid #{JSON.stringify file}" for file in @getRandomSongFiles add_count)
      @mpd.sendCommands commands, (msg) =>
        # track which ones are the automatic ones
        changed = false
        for line in msg.split("\n")
          [name, value] = line.split(": ")
          continue if name != "Id"
          @random_ids[value] = 1
          changed = true
        @onStatusChanged() if changed

    # scrub the random_ids
    new_random_ids = {}
    for id of @random_ids
      if all_ids[id]
        new_random_ids[id] = 1
    @random_ids = new_random_ids
    @previous_ids = all_ids
    @onStatusChanged()

  updateStickers: =>
    @mpd.sendCommand "sticker find song \"/\" \"#{LAST_QUEUED_STICKER}\"", (msg) =>
      current_file = null
      for line in msg.split("\n")
        [name, value] = mpd.split_once line, ": "
        if name == "file"
          current_file = value
        else if name == "sticker"
          value = mpd.split_once(value, "=")[1]
          track = @mpd.library.track_table[current_file]
          if track?
            track.last_queued = new Date(value)
          else
            @log.error "#{current_file} has a last-queued sticker of #{value} but we don't have it in our library cache."
      @got_stickers = true

  getRandomSongFiles: (count) =>
    return [] if count == 0
    never_queued = []
    sometimes_queued = []
    for _, track of @mpd.library.track_table
      if track.last_queued?
        sometimes_queued.push track
      else
        never_queued.push track
    # backwards by time
    sometimes_queued.sort (a, b) =>
      b.last_queued.getTime() - a.last_queued.getTime()
    # distribution is a triangle for ever queued, and a rectangle for never queued
    #    ___
    #   /| |
    #  / | |
    # /__|_|
    max_weight = sometimes_queued.length
    triangle_area = Math.floor(max_weight * max_weight / 2)
    rectangle_area = max_weight * never_queued.length
    total_size = triangle_area + rectangle_area
    # decode indexes through the distribution shape
    files = []
    for i in [0...count]
      index = Math.random() * total_size
      if index < triangle_area
        # triangle
        track = sometimes_queued[Math.floor Math.sqrt index]
      else
        # rectangle
        track = never_queued[Math.floor((index - triangle_area) / max_weight)]
      files.push track.file
    files

