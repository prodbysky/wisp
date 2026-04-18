# Wisp
Kinda weird local music player, that I made because (IN MY OPINION)
most linux players suck, tauon is slow, strawberry uses QT, VLC feels bad to use

## Building
```bash
    gcc nob.c -o nob
    ./nob
```

### Dependencies
- libFLAC
- X11

## Usage
By default wisp assumes $(HOME)/Music is where your music is stored.
If you desire to put it else where pass the path (where your music is) as the first argument to wisp.

## Config
Wisp will search for its .conf file in $(HOME)/.config/wisp.conf
The config is line based. And for now it only can configure your music root path.
To specify it:
wisp.conf
```
library_path /home/shr/Downloaded
```

### Keybinds
 - `h`       : move back to the album select column
 - `j`       : move down in the current column
 - `k`       : move up in the current column
 - `l`       : move to the track select column
 - `return`  : play the selected track
 - `q`       : queue the selected track
 - `S-q`     : queue the selected album from the current selected track
 - `tab`     : switch to next pane (main->queue->dft)
 - `space`   : toggle playing state
 - `.`       : skip current track forward
 - `,`       : skip current track backward
 - `>`       : seek playing track forwards by 5 seconds
 - `<`       : seek playing track backward by 5 seconds
 - `C-s`     : enable shuffling
 - `C-r`     : cycle loop mode

