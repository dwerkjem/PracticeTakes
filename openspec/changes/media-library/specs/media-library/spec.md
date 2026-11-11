## ADDED Requirements

### Requirement: The library is one folder of songs and takes
The application SHALL keep a library folder holding a `Songs` folder and a
`Takes` folder. It SHALL default to `Practice Takes` under the user's Music
folder, and SHALL be changeable by the user. Changing it SHALL move nothing.

#### Scenario: First use
- **WHEN** the application saves its first take or adds its first song
- **THEN** the library folder and the folder the item goes in are created

#### Scenario: Pointing elsewhere and back
- **WHEN** the user changes the library folder and later changes it back
- **THEN** every song and take in the first folder is listed again

### Requirement: The shipped sample is always in the library
The Songs list SHALL include the bundled sample score as a built-in entry,
which SHALL NOT be removable.

#### Scenario: An empty library
- **WHEN** the library folder holds no songs
- **THEN** the Songs list shows the sample, and nothing offers to remove it

### Requirement: The Library window lists songs and takes
File SHALL offer Library…, which SHALL open a window with a Songs list and a
Takes list. A song SHALL be listed with its title, composer, parts and when it
was added. A take SHALL be listed with when it was recorded, its song, its
length, the inputs it used, and how it ended, newest first.

#### Scenario: A song that does not import
- **WHEN** a file in `Songs` fails to import
- **THEN** it is listed by its file name and marked as unreadable, not hidden

#### Scenario: A take of a deleted song
- **WHEN** a take's song has been removed from the library
- **THEN** the take is still listed under the song's title and can still be
  reviewed

### Requirement: Songs open and takes are reviewed from the library
Opening a song from the library SHALL import it as Open Score does. Reviewing a
take SHALL make the take's own copy of its score the current score, and SHALL
show the take in the Take Review tool, judged with the current settings.

#### Scenario: Review a take from yesterday
- **WHEN** the user reviews a take saved in an earlier session
- **THEN** its score is open, the Take Review tool shows it, and the Score View
  marks its notes

### Requirement: Removing is recoverable
Removing a song or deleting a take SHALL ask for confirmation naming the item,
and SHALL move its files to the system's rubbish bin rather than erase them.

#### Scenario: Delete a take
- **WHEN** the user deletes a take and confirms
- **THEN** its folder is in the rubbish bin and the take is no longer listed

### Requirement: A missing library folder is reported, not replaced
When the library folder cannot be read or created, the Library window SHALL
say so and name the folder, and the application SHALL carry on. A take that
cannot be saved SHALL be reported, not lost silently.

#### Scenario: A detached drive
- **WHEN** the library folder is on a drive that is not attached
- **THEN** the Library window says the folder cannot be reached, and recording
  a take reports that it could not be saved

### Requirement: Captures use their own library
An instance started with `--test-control` SHALL use a temporary library folder
of its own, and SHALL NOT read or write the user's library.

#### Scenario: A capture run records a take
- **WHEN** a capture state records a take
- **THEN** the take is saved under that instance's temporary folder only
