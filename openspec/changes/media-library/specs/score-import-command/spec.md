## ADDED Requirements

### Requirement: A score opened from elsewhere is copied into the library
Opening a score from outside the library folder SHALL, once it imports, copy
the file into the library's `Songs` folder and record the copy as the open
file. A file with the same bytes already in `Songs` SHALL be reused rather than
copied again; a different file with the same name SHALL be copied under a
numbered name. A score already in the library SHALL be opened in place. A file
that fails to import SHALL NOT be copied.

#### Scenario: Open a new score
- **WHEN** a user opens a MusicXML file from their downloads
- **THEN** it opens, and the Songs list shows it

#### Scenario: Open the same score again
- **WHEN** the user opens the same downloaded file a second time
- **THEN** `Songs` still holds one copy of it

### Requirement: File offers the library
The File menu SHALL offer Library…, which opens the Library window.

#### Scenario: From the menu
- **WHEN** the user chooses File → Library…
- **THEN** the Library window opens, or comes to the front if it is open
