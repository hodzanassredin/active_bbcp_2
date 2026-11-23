Links & Targets

The Links & Targets dialog is used for inspecting and, if the text document is editable, setting the properties of links and targets. Links and targets are text stretches within pairs of link or target objects that can be made visible with the menu item Text -> Show Marks and hidden with Text -> Hide Marks.
This dialog can be opened by pressing the Ctrl key together with the right mouse button on a link or target. Alternatively it can be opened by selecting a text stretch containing links or targets and executing the Properties context menu item.

Link Properties
Type:	The read-only value Link shows that a link has been selected.
Link:	The command or semicolon-separated command sequence associated with this link. Executing the link means to execute this command or command sequence. Please note that string arguments must be enclosed in single quotes. 
Important commands used in a link are:
StdCmds.OpenBrowser('<pathAndName>', '<title>')
Open the specified file with the specified window title in browser mode, i.e. read-only.
StdCmds.OpenDoc('<pathAndName>')
Open the specified file as an editable document.
StdLinks.ShowTarget('<targetIdentifier>')
Scroll to the specified target within the current document.
Dialog.OpenExternal('<pathAndName>')
Open the specified file with the platform's default mechanism for the file's file name extension. This can also be used for opening a web URL by using the 'http:' or 'https:' prefix.
Examples:
StdCmds.OpenBrowser('System/Rsrc/Menus', 'system menus')
StdCmds.OpenDoc('System/Rsrc/Menus')
StdLinks.ShowTarget('section1')
Dialog.OpenExternal('http://blackboxframework.org')
Close:	Specifies if the document containg the link is closed when the link is executed.
always means to always close the document.
if Shift-Key is pressed means to close the document only when the Shift key is pressed. This is the default behavior.
never means to never close the document even if the Shift key is pressed. This is appropriate for example when scrolling to a different text position within the same document with the StdLinks.ShowTarget command.

Target Properties
Type:	The read-only value Target shows that a target has been selected.
Target:	The target identifier that can be used to scroll to this target by means of a StdLinks.ShowTarget('<targetIdentifier>') command.

Command Buttons
OK	Applies the properties to the selected view(s) and closes the dialog box.
Cancel	Closes the dialog box without applying the properties.
Apply	Like OK but does not close the dialog box.
Help	Opens this help text.
