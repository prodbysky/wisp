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

### Keybinds
`h`       - move album select head to the left
`l`       - move album select head to the right
`j`       - select next track within album
`k`       - select previous track within album
`enter`   - play the selected track
`q`       - enqueue the selected track
`Shift-q` - enqueue the selected albums tracks from the selected track onwards
`Tab`     - open other pane (if on albums view opens queue, and vice-versa)
`Space`   - pause/unpause
`left`    - seek backwards by 5 sec.
`right`   - seek forwards by 5 sec.
