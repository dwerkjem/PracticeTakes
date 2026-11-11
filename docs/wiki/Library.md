# Library

The library is where your songs and takes go: one folder on your computer that
Practice Takes keeps them in, and a window that lists them. Open it with
**File → Library…**.

## Where it is

The library is a folder called **Practice Takes** in your Music folder, with two
folders inside:

| Folder | Holds |
|---|---|
| `Songs` | Every score you have opened |
| `Takes` | Every take you have recorded, one folder each |

They are ordinary files, so you can see them, back them up and copy them to
another computer. To keep the library somewhere else, such as a larger drive,
choose **Change…** at the foot of the Library window. Nothing is moved when you
change it: the old folder stays as it was, and changing back finds everything
again. The folder belongs to this computer, so exporting your settings does not
carry it to another.

If the folder is on a drive that is not attached, the Library window says it
cannot be reached, and nothing is saved there until it is back.

## Songs

Opening a score with **File → Open score…** puts a copy of it in `Songs`, so it
is in the library from then on. The copy is made only once the score opens, so
a file that cannot be read is not copied. Opening the same file again does not
copy it twice. A different score with the same file name is copied as
`Name (2)`.

The **Songs** list gives each score's title, composer, parts and the date it
was added. The sample, Mahler's *Die zwei blauen Augen*, is always there,
marked *(built in)*. A file in `Songs` that cannot be read is still listed,
marked *(cannot be read)*, so you can see it and remove it.

| Button | Does |
|---|---|
| **Open** | Opens the song in the Score View (or double-click it) |
| **Add Songs…** | Copies scores into the library without opening them |
| **Remove…** | Moves the song's file to the bin. Its takes stay |
| **Show in Folder** | Shows the file in your file manager |

## Takes

Every take you record is saved in `Takes` when it ends: one that reaches the
end, one you stop early, and one that fails. A take you cancel before playing
anything is not saved.

A take's folder holds what was recorded (the microphone as a FLAC file, and the
keyboard's notes), when and at what tempo, and **its own copy of the score it was played
against**. So a take still opens, and is judged against the music as it was
when you played it, even after the song is changed or removed.

The **Takes** list is newest first, and gives when each take was recorded, its
song, how long it is, what it used (microphone, keyboard or both), how it ended
and its size. **Only takes of…** shows just the selected song's takes.

| Button | Does |
|---|---|
| **Review** | Opens the take's score and shows the take in [[Take Review\|Tools#take-review]], judged with your current settings (or double-click it) |
| **Play** | Reviews it and plays it back: what you recorded, with the score cursor following |
| **Export Audio…** | Saves its audio as a WAV file wherever you choose |
| **Delete…** | Moves the take's folder to the bin |
| **Show in Folder** | Shows the take's folder in your file manager |

Reviewing a take from an earlier session judges it again with today's pitch and
timing settings, as changing a setting does for a take of this session.

## Nothing is erased

**Remove** and **Delete** always ask first, and always move to your system's
bin, never erase. A recording cannot be made again, so getting one back should
be as easy as opening the bin.

Takes are never deleted for you, either. A three-minute take with the
microphone is about 10 MB, so a library of a year's practice can grow large;
the **Size** column shows what each take takes up.
