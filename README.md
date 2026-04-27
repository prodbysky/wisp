# Wisp

A keyboard-driven music player for Linux, built because most existing options felt slow, heavy, or awkward to use. Supports FLAC and MP3 with album art, playlists, crossfade, shuffle, and an FFT visualizer.

## Building

```bash
gcc nob.c -o nob
./nob
```

## Usage
```bash
./build/wisp                  # uses ~/Music by default
./build/wisp --path /path/to/music
./build/wisp --playlist-dir /path/to/playlists
./build/wisp --help
```

## Configuration

Wisp reads `~/.config/wisp.conf` on startup. The file is line-based:

```
library_path  /home/you/Music
playlist_dir  /home/you/.wisp/playlists
```

Command-line flags take precedence over the config file.

Playlists are stored as standard `.m3u` files in `~/.wisp/playlists/` by default, and are saved on exit.

## Interface

The UI is split into four panes, cycled with `Tab`:

- **Main** - two-column album/track browser
- **Queue** - current playback queue with history
- **Visual** - FFT spectrum visualizer, colored from the current album art
- **Playlist** - saved playlist browser

### Keybinds

#### Navigation

| Key | Action |
|-----|--------|
| `h` | Move to album column (from track column) |
| `j` | Move down |
| `k` | Move up |
| `l` | Move to track column (from album column) |
| `Tab` | Cycle to next pane |

#### Playback

| Key | Action |
|-----|--------|
| `Return` | Play selected track |
| `Space` | Toggle pause/resume |
| `Shift+.` | Skip to next track |
| `Shift+,` | Skip to previous track |
| `>` | Seek forward 5 seconds |
| `<` | Seek backward 5 seconds |
| `Ctrl+s` | Toggle shuffle |
| `Ctrl+r` | Cycle loop mode (none → one → all) |

#### Queue

| Key | Action |
|-----|--------|
| `q` | Queue selected track |
| `Shift+Q` | Queue album from selected track onwards |

#### Playlists

| Key | Action |
|-----|--------|
| `a` | Open playlist picker to add selected track |
| `Shift+A` | Open playlist picker to add album from selected track onwards |
| `Ctrl+n` | (in picker) Create a new playlist |

## Features

- **FLAC and MP3** support with full tag reading (title, artist, album, track number, embedded cover art)
- **Crossfade** between tracks (2-second smooth blend)
- **FFT visualizer** with colors derived from the current album's cover art via k-means clustering
- **Playlists** saved as standard `.m3u` files
- **Shuffle** and three **loop modes**: none, repeat one, repeat all
