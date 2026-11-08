# Troubleshooting

## The microphone

| You see | Try |
|---|---|
| A warning that no usable microphone is available | Connect one, then choose it in **Settings → Audio**. The warning's shortcut goes there |
| "Waiting for the microphone" | The device is opening or was just changed. If it doesn't clear, choose the device again in **Settings → Audio** |
| "Microphone muted", or an amber frame around the workspace | Click **Mic muted - Unmute** on the title bar, or unmute it in **Settings → Audio** |
| "Microphone disconnected" | Reconnect it. Practice Takes recovers the device by itself when it returns |
| "Microphone input is clipping" | Lower **Input volume** in **Settings → Audio**, or move back from the microphone |
| The tuner never settles | Play or sing a *sustained* note. A short or noisy sound gives it nothing to hold. **Settings → Practice** has presets for different material |

Only one application at a time can usually hold a microphone. If another program
has it, close that program first.

## MIDI keyboard

| You see | Try |
|---|---|
| "No MIDI input detected" | Connect the keyboard, then press **Rescan** in **Settings → MIDI** |
| *Keyboard* (not connected) | It was chosen before but isn't plugged in now. It will be used as soon as it appears |
| "Nothing received yet" | Play a key. If nothing arrives, check the keyboard's MIDI-over-USB setting and its cable |
| The pedal shows as *not measured* | Move the pedal once. A pedal only reports when it changes |

More in [[MIDI Keyboard]].

## Scores

| You see | Try |
|---|---|
| The import summary lists dropped notation | Expected: slurs, articulations, chord symbols and similar aren't drawn yet. See [[Scores and MusicXML]] |
| The file is refused as "timewise" | Re-export it from your notation program as standard (partwise) MusicXML |
| The file is refused as not MusicXML | Export as `.musicxml` or `.mxl`, not the program's own format |
| "The music font did not load" | The score is drawn without its notation glyphs. Please report it, with your platform |

## Settings and layouts

- **A layout has gone wrong:** **Tools → Workspaces → Reset to Pitch Practice...**
  returns the current arrangement to *Pitch Practice*. Named workspaces and
  preferences are kept.
- **An imported `.ptsettings` file is refused:** it came from a newer version of
  Practice Takes. Update, then import again.

## Still stuck?

Use **Help → Send feedback**, or open an issue on
[the issue tracker](https://github.com/dwerkjem/PracticeTakes/issues/new/choose).
Saying your platform, the version shown in the window title, and what you
expected to happen saves a round trip.
