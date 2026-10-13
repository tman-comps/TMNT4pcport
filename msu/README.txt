ORCHESTRAL / REMASTERED MUSIC (MSU-1 PACKS)

Put the .pcm files of an MSU-1 pack for Turtles in Time in this folder, then pick
Music: Orchestral in Menu -> Port settings -> Audio. (The option only appears when at
least one track listed in msu.txt has a file here.)

msu.txt says which game song plays which track number. Track N is played from any file
in this folder whose name ends in "-N.pcm" (e.g. "tmnt4-5.pcm" or "Turtles in Time-5.pcm").
If your pack numbers its tracks differently, edit the second column of msu.txt.

How songs are recognised: the game streams each song's data to the sound chip before
playing it; the first column of msu.txt is a checksum of that data, so a song is
recognised wherever it plays. Songs the game plays that aren't in msu.txt are written to
unmapped.txt in this folder as you play (with where they played), so you can add them.
Songs without a file just play the original SNES music, and the sound effects always
come from the game.
