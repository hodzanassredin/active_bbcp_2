StdStamps

DEFINITION StdStamps;

	PROCEDURE Deposit;

	...plus some other items used internally...

END StdStamps.


StdStamps are views which indicate (1) the date when the containing document has been saved the last time and (2) the number of document changes. Stamp views carry a sequence number and a fingerprint of the document with them. Each time the document (and therefore its fingerprint) is changed and stored, the sequence number is incremented. When determining the fingerprint of the document, whitespace is ignored, except in string literals. This behavior makes StdStamps particularily useful for program texts.

Each stamp view also keeps track of the history of the most recent changes. For up to 25 entries, the date and time, and an optional one-line comment is stored. To avoid too many entries in the history while working on a document, the most recent history entry is overwritten upon the generation of a new sequence number if the current date is the same as the date in the most recent history entry.

To avoid exceeding 25 entries the oldest uncommented entry is deleted or the oldest if they are all commented.


PROCEDURE Deposit
Deposit command for standard stamps.

...plus some other items used internally...

Clicking on the stamp view shows a history of when the document has been saved.

Ctrl-clicking on the stamp view opens a dialog box that allows to edit the comment associated with the current and previous sessions. Specifying a Comment for a new sequence number (Seq. Nr.) always adds a new entry to the history even if fingerprint and date are unchanged. Selecting an existing sequence number allows to modify the comment of a previous session. Any comment editing is made persistent only when the document is saved the next time.


