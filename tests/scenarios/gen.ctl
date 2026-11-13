# the generators: SEQ twice opens Generate; its pages on SELECT; a part plays with PLAY, in time, and stops clean
tap SEQ
expect view 3
tap SEQ
expect view 6
expect gen_pg 0
shot gen_song.png
# KNOB 1: the style (P-Funk is the default; two on: Electro)
spin K1 2
expect gen_style 12
spin SELECT 1
wait 50
spin SELECT 1
wait 50
spin SELECT 1
wait 50
expect gen_pg 3
# the drums on; nothing sounds until PLAY
spin K1 1
expect gen_on0 1
out
wait 300
expect notes_out 0
# PLAY: the drums play (the Sequencer page's own tracks are muted by default)
tap PLAY
wait 2050
expect gen_bar 1
expect notes_out >20
shot gen_drums.png
# the bass too: its page is three on; FX held shows its channel and device
spin SELECT 1
wait 50
spin SELECT 1
wait 50
spin SELECT 1
wait 50
expect gen_pg 6
spin K1 1
expect gen_on3 1
press FX
wait 50
shot gen_bass_setup.png
release FX
expect view 6
# REC: the part's next variation
tap REC
expect gen_var3 1
wait 4000
shot gen_bass.png
tap PLAY
wait 50
expect notes_on 0
# REC on a song page: a new seed
spin SELECT -1
wait 50
spin SELECT -1
wait 50
spin SELECT -1
wait 50
spin SELECT -1
wait 50
spin SELECT -1
wait 50
spin SELECT -1
wait 50
expect gen_pg 0
tap REC
expect gen_seed >0
# external clock: the generators follow MIDI clock from the TRS jack, 24 a quarter note
tap GLO
spin K2 2
tap SEQ
tap SEQ
out
wait 100
expect notes_out 0
midi TRS FA
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
midi TRS F8
wait 21
# two quarter notes of clock: the kick on each, hats between
expect notes_out >5
expect gen_bar 0
midi TRS FC
wait 30
expect notes_on 0
# the black keys: the parts (the fourth is the bass, on from before)
tapkey 8
expect gen_on3 0
tapkey 8
expect gen_on3 1
